import {EventEmitter} from 'node:events';
import {WebSocket} from 'ws';
import {Command, HitEvent, Message} from './types';

export class ConnectionManager extends EventEmitter {
    #ws: WebSocket | null = null;
    #disconnectExpected: boolean = false;

    connect(address: string, password: string): Promise<boolean> {
        this.disconnect();
        return new Promise(resolve => {
            this.#disconnectExpected = true;
            this.#ws = new WebSocket(`ws://${address}/?password=${encodeURIComponent(password)}`, {
                handshakeTimeout: 3000
            })
                .on('open', this.#onConnectionOpen.bind(this))
                .on('close', this.#onConnectionClosed.bind(this))
                .on('error', this.#onConnectionClosed.bind(this))
                .on('message', this.#onMessage.bind(this));
            this.once('connected', () => resolve(true));
            this.once('disconnected', () => resolve(false));
        });
    }

    disconnect() {
        if (this.#ws) {
            this.#disconnectExpected = true;
            this.#ws.close();
            this.#ws = null;
        }
    }

    #onConnectionOpen() {
        this.emit('connected');
        this.#disconnectExpected = false;
    }

    #onConnectionClosed() {
        this.emit('disconnected', this.#disconnectExpected);
        this.#disconnectExpected = false;
    }

    #onMessage(data: Buffer) {
        let jsonData = null;
        try {
            jsonData = JSON.parse(data.toString());
        } catch (error) {
            this.emit('error', new Error('Failed to parse message body from the server.'));
            return;
        }
        const message: Message = jsonData;
        switch (message.type) {
            case 'error':
                this.emit('error', new Error(`Action unsuccessful: ${message.message}`));
                break;
            case 'add':
                this.emit('add', [message.breakpoint]);
                break;
            case 'remove':
                this.emit('remove', message.id);
                break;
            case 'hit':
                const hitEvent: HitEvent = {
                    hit: {
                        id: message.id,
                        date: new Date().toISOString(),
                        values: message.values
                    },
                    breakpointId: message.breakpointId
                };
                this.emit('hit', [hitEvent]);
                break;
            case 'breakpoints':
                this.emit('add', message.breakpoints);
                break;
            case 'hits':
                this.emit('hit', message.hits.map(hit => ({
                    breakpointId: message.breakpointId,
                    hit: hit
                })));
                break;
            default:
                this.emit('error', new Error('Unknown message type received from the server.'));
                break;
        }
    }

    waitForEvent<T>(eventName: string): Promise<T> {
        return new Promise((resolve, reject) => {
            this.once(eventName, data => resolve(data));
            this.once('error', error => reject(error));
        });
    }

    send(command: Command) {
        if (this.#ws === null) {
            throw new Error('There is no active Cloudebug connection to send to!');
        }
        this.#ws.send(JSON.stringify(command));
    }

    isConnected(): boolean {
        return this.#ws !== null;
    }
}
