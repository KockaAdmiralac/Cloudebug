from asyncio import AbstractEventLoop, Queue
from dataclasses import dataclass
from typing import List, Optional, Tuple

MQueue = Queue[Optional[Tuple[int, List[str]]]]

@dataclass
class State:
    message_queue: MQueue
    event_loop: AbstractEventLoop

state: Optional[State] = None

def get_state() -> State:
    if state is None:
        raise Exception('State not set before retrieving. This should never happen.')
    return state

def set_state(new_state: State):
    global state
    state = new_state
