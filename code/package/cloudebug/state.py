from asyncio import AbstractEventLoop
from dataclasses import dataclass
from typing import Optional

from websockets.server import WebSocketServer

@dataclass
class State:
    ws_server: WebSocketServer
    event_loop: AbstractEventLoop

state: Optional[State] = None

def get_state() -> State:
    if state is None:
        raise Exception('State not set before retrieving. This should never happen.')
    return state

def set_state(new_state: State):
    global state
    state = new_state
