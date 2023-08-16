export interface BreakpointData {
    id: number;
    file: string;
    line: number;
    condition: string | null;
    expressions: string[];
}

interface HitData {
    id: number;
    date: string;
    values: string[]
}

interface ErrorMessage {
    type: 'error';
    message: string;
}

interface BreakpointAddedMessage {
    type: 'add';
    breakpoint: BreakpointData;
}

interface GetBreakpointsMessage {
    type: 'breakpoints';
    breakpoints: BreakpointData[]
}

interface GetHitsMessage {
    type: 'hits';
    breakpointId: number;
    hits: HitData[];
}

interface BreakpointRemovedMessage {
    type: 'remove';
    id: number;
}

interface Hit {
    id: number;
    breakpointId: number;
    values: string[];
}

interface HitMessage {
    type: 'hit';
    hits: Hit[];
}

export type Message = ErrorMessage |
                      BreakpointAddedMessage |
                      BreakpointRemovedMessage |
                      GetBreakpointsMessage |
                      GetHitsMessage |
                      HitMessage;

interface AddBreakpointCommand {
    type: 'add';
    line: number;
    file: string;
    condition: string | undefined;
    expressions: string[] | undefined;
}

interface GetBreakpointsCommand {
    type: 'breakpoints';
}

interface GetHitsCommand {
    type: 'hits';
    id: number;
}

interface RemoveBreakpointCommand {
    type: 'remove';
    id: number;
}

export type Command = AddBreakpointCommand |
                      GetBreakpointsCommand |
                      GetHitsCommand |
                      RemoveBreakpointCommand;

export interface HitEvent {
    hit: HitData;
    breakpointId: number;
}
