from asyncio import Event, new_event_loop, run_coroutine_threadsafe
from json import dumps
from sys import _getframe, stderr
from threading import Thread
from traceback import format_exception
from typing import Any, Callable, List

from .db import CloudebugDB
from .state import get_state
from .util import get_base_path
from .ws import cloudebug_thread, init_breakpoints

def breakpoint_callback(id: int):
    # TODO: Cached evaluation of expressions
    try:
        state = get_state()
        db = CloudebugDB(get_base_path())
        prev_frame = _getframe().f_back
        values: List[str] = []
        breakpoint = db.get_breakpoint(id)
        if breakpoint is None:
            # Should never happen
            print('Breakpoint that was hit is not in the database', file=stderr)
            return
        condition = breakpoint[3]
        if prev_frame is not None:
            if condition is not None:
                try:
                    if not eval(condition, prev_frame.f_globals, prev_frame.f_locals):
                        # Condition false, do not log
                        return
                except Exception as error:
                    # Condition failed, do not log
                    print('Condition evaluation failed:', ''.join(format_exception(error)), file=stderr)
                    return
            for expression in db.get_expressions(id):
                try:
                    values.append(str(eval(expression, prev_frame.f_globals, prev_frame.f_locals)))
                except Exception as error:
                    print('Expression evaluation failed:', ''.join(format_exception(error)), file=stderr)
                    values.append('<error>')
        hit_id = db.log_hit(id, values)
        for socket in state.ws_server.websockets:
            run_coroutine_threadsafe(socket.send(dumps({
                'type': 'hit',
                'id': hit_id,
                'breakpointId': id,
                'values': values
            })), state.event_loop)
    except Exception as error:
        print('Unknown error:', ''.join(format_exception(error)), file=stderr)

def init(main_func: Callable[[], Any]):
    init_breakpoints()
    event_loop = new_event_loop()
    exit_event = Event()
    side_thread = Thread(target=cloudebug_thread, args=(exit_event, event_loop))
    side_thread.start()
    try:
        main_func()
    finally:
        event_loop.call_soon_threadsafe(exit_event.set)
