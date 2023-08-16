from asyncio import AbstractEventLoop
from http import HTTPStatus
from json import JSONDecodeError, dumps, loads
from os import environ
from typing import Any, Awaitable, Callable, Dict, Optional, Tuple
from urllib.parse import parse_qs, urlparse

from websockets.datastructures import HeadersLike
from websockets.exceptions import ConnectionClosedOK
from websockets.legacy.server import serve
from websockets.server import WebSocketServerProtocol

from .db import Breakpoint, CloudebugDB, Hit
from .state import MQueue, State, set_state
from .util import deject, get_base_path, inject

def breakpoint_to_json(breakpoint: Breakpoint) -> Dict[str, Any]:
    id, file, line, condition, expressions = breakpoint
    return {
        'id': id,
        'file': file,
        'line': line,
        'condition': condition,
        'expressions': expressions
    }

def hit_to_json(hit: Hit) -> Dict[str, Any]:
    id, date, values = hit
    return {
        'id': id,
        'date': date.isoformat(),
        'values': values
    }

async def handle_add(websocket: WebSocketServerProtocol, message: Dict[str, Any], db: CloudebugDB):
    if not 'line' in message or not 'file' in message:
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Missing file or line to insert a breakpoint on.'
        }))
        return
    line = message['line']
    file = message['file']
    condition = message.get('condition', None)
    expressions = message.get('expressions', [])
    if not isinstance(line, int) or not isinstance(file, str):
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Malformed file or line to insert a breakpoint on.'
        }))
        return
    if not isinstance(expressions, list) or any([not isinstance(expr, str) for expr in expressions]):
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Expressions must be supplied as an array of strings.'
        }))
        return
    if condition is not None and not isinstance(condition, str):
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Condition must be supplied as a string.'
        }))
        return
    breakpoint_id = db.add_breakpoint(file, line, condition, expressions)
    breakpoint = db.get_breakpoint(breakpoint_id)
    if breakpoint is None:
        # This should never happen
        return
    # TODO: Handle errors
    inject(get_base_path(), file, line, breakpoint_id)
    await websocket.send(dumps({
        'type': 'add',
        'breakpoint': breakpoint_to_json(breakpoint)
    }))

async def handle_breakpoints(websocket: WebSocketServerProtocol, message: Dict[str, Any], db: CloudebugDB):
    await websocket.send(dumps({
        'type': 'breakpoints',
        'breakpoints': [breakpoint_to_json(b) for b in db.get_breakpoints()]
    }))

async def handle_hits(websocket: WebSocketServerProtocol, message: Dict[str, Any], db: CloudebugDB):
    if not 'id' in message or not isinstance(message['id'], int):
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Breakpoint ID to remove not specified or not an integer.'
        }))
        return
    await websocket.send(dumps({
        'type': 'hits',
        'breakpointId': message['id'],
        'hits': [hit_to_json(h) for h in db.get_hits(message['id'])]
    }))

async def handle_remove(websocket: WebSocketServerProtocol, message: Dict[str, Any], db: CloudebugDB):
    if not 'id' in message or not isinstance(message['id'], int):
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Breakpoint ID to remove not specified or not an integer.'
        }))
        return
    id = message['id']
    breakpoint_data = db.get_breakpoint(id)
    if breakpoint_data is None:
        await websocket.send(dumps({
            'type': 'error',
            'message': 'Breakpoint with the specified ID does not exist.'
        }))
        return
    id, file, line, condition, expressions = breakpoint_data
    db.remove_breakpoint(id)
    # TODO: Handle errors
    deject(get_base_path(), file, line, id)
    await websocket.send(dumps({
        'type': 'remove',
        'id': id
    }))

WSHandler = Callable[[WebSocketServerProtocol, Dict[str, Any], CloudebugDB], Awaitable[Any]]
HANDLERS: Dict[str, WSHandler] = {
    'add': handle_add,
    'breakpoints': handle_breakpoints,
    'hits': handle_hits,
    'remove': handle_remove
}

async def cloudebug_ws_handler(websocket: WebSocketServerProtocol):
    db = CloudebugDB(get_base_path())
    while not websocket.closed:
        try:
            message = loads(await websocket.recv())
        except JSONDecodeError:
            await websocket.send(dumps({
                'type': 'error',
                'message': 'Invalid JSON body.'
            }))
            continue
        except ConnectionClosedOK:
            break
        if not 'type' in message or not message['type'] in HANDLERS:
            await websocket.send(dumps({
                'type': 'error',
                'message': 'Invalid message type.'
            }))
            continue
        await HANDLERS[message['type']](websocket, message, db)

async def process_request(path: str, headers: HeadersLike) -> Optional[Tuple[HTTPStatus, HeadersLike, bytes]]:
    if 'CLOUDEBUG_PASSWORD' not in environ:
        return
    passwords = parse_qs(urlparse(path).query).get('password', [])
    if len(passwords) == 0 or passwords[0] != environ['CLOUDEBUG_PASSWORD']:
        return HTTPStatus.FORBIDDEN, {}, dumps({
            'type': 'error',
            'message': 'Authentication failed.'
        }).encode('utf-8')

async def cloudebug_main(message_queue: MQueue, event_loop: AbstractEventLoop):
    async with serve(cloudebug_ws_handler, 'localhost', 19287, process_request=process_request) as server:
        set_state(State(message_queue, event_loop))
        db = CloudebugDB(get_base_path(), True)
        while True:
            item = await message_queue.get()
            if item is None:
                break
            breakpoint_ids = [item[0]]
            values = [item[1]]
            for i in range(message_queue.qsize()):
                item = message_queue.get_nowait()
                if item is None:
                    return
                breakpoint_ids.append(item[0])
                values.append(item[1])
            hit_ids = db.log_hits(breakpoint_ids, values)
            hits = [{
                'id': hit_id,
                'breakpointId': breakpoint_ids[idx],
                'values': values[idx]
            } for idx, hit_id in enumerate(hit_ids)]
            for socket in server.websockets:
                await socket.send(dumps({
                    'type': 'hit',
                    'hits': hits
                }))

def cloudebug_thread(message_queue: MQueue, event_loop: AbstractEventLoop):
    event_loop.run_until_complete(cloudebug_main(message_queue, event_loop))

def init_breakpoints():
    base = get_base_path()
    db = CloudebugDB(base, True)
    for id, file, line, condition, expressions in db.get_breakpoints():
        inject(base, file, line, id)
