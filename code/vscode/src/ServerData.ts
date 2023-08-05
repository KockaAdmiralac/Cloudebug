import {EventEmitter} from 'node:events';
import {BreakpointData, HitEvent} from './types';

export class ServerData extends EventEmitter {
    #breakpoints: BreakpointData[] = [];
    #hits: Record<number, HitEvent[]> = {};

    addBreakpoints(breakpoints: BreakpointData[]) {
        this.#breakpoints.push(...breakpoints);
        this.emit('add', breakpoints);
    }

    removeBreakpoints(breakpoints: number[]) {
        this.#breakpoints = this.#breakpoints
            .filter(b => !breakpoints.includes(b.id));
        this.emit('remove', breakpoints);
    }

    addHits(hits: HitEvent[]) {
        for (const hit of hits) {
            this.#hits[hit.breakpointId] = this.#hits[hit.breakpointId] || [];
            this.#hits[hit.breakpointId].push(hit);
        }
        this.emit('hits', hits);
    }

    clear() {
        this.removeBreakpoints(this.#breakpoints.map(b => b.id));
    }

    getBreakpointsInFile(file: string) {
        return this.#breakpoints.filter(b => b.file === file);
    }
}
