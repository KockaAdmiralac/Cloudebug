import * as vscode from 'vscode';
import {ConnectionManager} from './ConnectionManager';
import {ServerData} from './ServerData';
import {BreakpointData, HitEvent} from './types';

export class BreakpointTreeItem extends vscode.TreeItem {
    constructor(public data: BreakpointData) {
        const {id, condition, file, line} = data;
        const collapsibleState = vscode.TreeItemCollapsibleState.Collapsed;
        const label = `${id}`;
        super(label, collapsibleState);
        const conditionText = (
            typeof condition === 'undefined' ||
            condition === null
        ) ?
            'no condition' :
            `the condition of "${condition}"`;
        this.tooltip = `Breakpoint ID ${id} in file ${data.file} at line ${line} with ${conditionText}`;
        this.description = `${file}:${line}`;
        this.contextValue = 'breakpoint';
        this.iconPath = new vscode.ThemeIcon('debug-breakpoint');
    }
}

export class HitTreeItem extends vscode.TreeItem {
    constructor(public data: HitEvent, public parent: BreakpointTreeItem) {
        const {hit: {id, date, values}, breakpointId} = data;
        const collapsibleState = values.length === 0 ?
            vscode.TreeItemCollapsibleState.None :
            vscode.TreeItemCollapsibleState.Collapsed;
        const label = `${id}`;
        super(label, collapsibleState);
        this.tooltip = `Hit ID ${id} of the breakpoint ${breakpointId} on date ${date} with ${values.length} evaluated expressions.`;
        this.description = `${date} [${values.length} values]`;
        this.contextValue = 'breakpoint-hit';
        this.iconPath = new vscode.ThemeIcon('check');
    }
}

export class ExpressionValueTreeItem extends vscode.TreeItem {
    constructor(public label: string, public description: string,
        public parent: HitTreeItem) {
        super(label, vscode.TreeItemCollapsibleState.None);
        this.label = `${this.label}:`;
        this.tooltip = description;
        this.contextValue = 'expression-value';
    }
}

type TreeItem = BreakpointTreeItem | HitTreeItem | ExpressionValueTreeItem;

export class BreakpointsTreeDataProvider implements vscode.TreeDataProvider<TreeItem> {
    private treeDataEmitter: vscode.EventEmitter<TreeItem | void> =
        new vscode.EventEmitter<TreeItem | void>();
    onDidChangeTreeData: vscode.Event<TreeItem | void> =
        this.treeDataEmitter.event;
    private breakpoints: BreakpointTreeItem[] = [];
    private hits: Record<number, HitTreeItem[]> = {};
    private expressionValues: Record<number, ExpressionValueTreeItem[]> = {};

    constructor(
        serverData: ServerData,
        private connectionManager: ConnectionManager
    ) {
        serverData.on('add', (breakpoints: BreakpointData[]) => {
            this.breakpoints.push(...breakpoints
                .map(b => new BreakpointTreeItem(b)));
            this.treeDataEmitter.fire();
        }).on('remove', (breakpoints: number[]) => {
            this.breakpoints = this.breakpoints
                .filter(b => !breakpoints.includes(b.data.id));
            for (const breakpointId of breakpoints) {
                for (const hit of (this.hits[breakpointId] || [])) {
                    delete this.expressionValues[hit.data.hit.id];
                }
                delete this.hits[breakpointId];
            }
            this.treeDataEmitter.fire();
        }).on('hits', (hits: HitEvent[]) => {
            const breakpointsToUpdate: BreakpointTreeItem[] = [];
            for (const hit of hits) {
                const {breakpointId} = hit;
                const breakpoint = this.breakpoints
                    .find(b => b.data.id === breakpointId);
                if (!breakpoint) {
                    // ???
                    return;
                }
                const hitItem = new HitTreeItem(hit, breakpoint);
                this.hits[breakpointId] = this.hits[breakpointId] || [];
                this.hits[breakpointId].push(hitItem);
                if (!breakpointsToUpdate.includes(breakpoint)) {
                    breakpointsToUpdate.push(breakpoint);
                }
                this.registerExpressionValues(hitItem);
            }
            for (const breakpoint of breakpointsToUpdate) {
                this.treeDataEmitter.fire(breakpoint);
            }
        });
    }

    getTreeItem(element: TreeItem): vscode.TreeItem {
        return element;
    }

    getChildren(element?: TreeItem): vscode.ProviderResult<TreeItem[]> {
        if (!element) {
            return this.breakpoints;
        } else if (element instanceof BreakpointTreeItem) {
            return this.hits[element.data.id];
        } else if (element instanceof HitTreeItem) {
            return this.expressionValues[element.data.hit.id];
        }
        return [];
    }

    getParent(element: TreeItem): vscode.ProviderResult<TreeItem> {
        if (element instanceof BreakpointTreeItem) {
            return;
        }
        return element.parent;
    }

    private registerExpressionValues(hit: HitTreeItem) {
        this.expressionValues[hit.data.hit.id] = [];
        for (const [index, value] of Object.entries(hit.data.hit.values)) {
            const expression = hit.parent.data.expressions[Number(index)];
            this.expressionValues[hit.data.hit.id]
                .push(new ExpressionValueTreeItem(expression, value, hit));
        }
    }

    async resolveTreeItem(_item: vscode.TreeItem, element: BreakpointTreeItem,
        _token: vscode.CancellationToken): Promise<null> {
        if (
            !(element instanceof BreakpointTreeItem) ||
            !this.connectionManager.isConnected()
        ) {
            return null;
        }
        const breakpointId = element.data.id;
        if (this.hits[breakpointId]) {
            // We already have this data.
            return null;
        }
        this.connectionManager.send({
            type: 'hits',
            id: breakpointId
        });
        const hits = await this.connectionManager.waitForEvent('hits');
        if (Array.isArray(hits)) {
            this.hits[breakpointId] = hits
                .map(data => new HitTreeItem(data, element));
            for (const hit of hits) {
                this.registerExpressionValues(hit);
            }
            this.treeDataEmitter.fire(element);
        }
        return null;
    }
}
