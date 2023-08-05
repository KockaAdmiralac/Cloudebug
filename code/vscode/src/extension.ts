import * as vscode from 'vscode';
import {
    BreakpointTreeItem,
    BreakpointsTreeDataProvider
} from './BreakpointsTreeDataProvider';
import {ConnectionManager} from './ConnectionManager';
import {ServerData} from './ServerData';
import {BreakpointData, HitEvent} from './types';

const error = vscode.window.showErrorMessage;
const info = vscode.window.showInformationMessage;
const input = vscode.window.showInputBox;
const win = vscode.window;
const cmd = vscode.commands;

const connectionManager = new ConnectionManager();
const serverData = new ServerData();
const breakpointsTree = new BreakpointsTreeDataProvider(
    serverData,
    connectionManager
);

const DEFAULT_ADDRESS = 'localhost:19287';

function getRelativeFileName(): string | void {
    if (
        !win.activeTextEditor ||
        !vscode.workspace.workspaceFolders ||
        vscode.workspace.workspaceFolders.length > 1
    ) {
        return;
    }
    const folderPath = vscode.workspace.workspaceFolders[0].uri.fsPath;
    const filePath = win.activeTextEditor.document.fileName;
    const relativeFilePath = filePath.replace(`${folderPath}/`, '');
    return relativeFilePath;
}

function updateDecorations(decoration: vscode.TextEditorDecorationType) {
    const editor = win.activeTextEditor;
    const fileName = getRelativeFileName();
    if (!fileName || !editor) {
        return;
    }
    const decorations: vscode.DecorationOptions[] = serverData
        .getBreakpointsInFile(fileName)
        .map(breakpoint => ({
            range: new vscode.Range(
                new vscode.Position(breakpoint.line - 1, 0),
                new vscode.Position(breakpoint.line - 1, 0)
            ),
            hoverMessage: `Breakpoint ID ${breakpoint.id}`
        }));
    editor.setDecorations(decoration, decorations);
}

function getCurrentLine(): number {
    if (!win.activeTextEditor) {
        return 0;
    }
    return win.activeTextEditor.selection.active.line + 1;
}

function getBreakpointOnLine(): BreakpointData | void {
    const fileName = getRelativeFileName();
    if (!fileName) {
        return;
    }
    const currentLine = getCurrentLine();
    const breakpointOnLine = serverData
        .getBreakpointsInFile(fileName)
        .find(b => b.line === currentLine);
    return breakpointOnLine;
}

async function connectCmd() {
    const enteredAdddress = await input({
        title: 'Enter the Cloudebug server\'s address',
        placeHolder: DEFAULT_ADDRESS
    });
    if (typeof enteredAdddress === 'undefined') {
        // User cancelled action.
        return;
    }
    const address = enteredAdddress === '' ?
        DEFAULT_ADDRESS :
        enteredAdddress;
    const password = await input({
        title: `Enter the password for the Cloudebug server at ${address}`,
        password: true
    });
    if (typeof password === 'undefined') {
        // User cancelled action.
        return;
    }
    info(`Connecting to ${address}...`);
    const connectionSuccessful = await connectionManager
        .connect(address, password);
    if (!connectionSuccessful) {
        error(`Failed to connect to the Cloudebug server at ${address}.`);
        return;
    }
    info(`Connected to the Cloudebug server at ${address}.`);
    connectionManager.send({
        type: 'breakpoints'
    });
}

async function addCmd(updateDecorationsFunc: () => void) {
    const fileName = getRelativeFileName();
    if (!fileName) {
        error('There must be exactly one folder open to set a breakpoint.');
        return;
    }
    if (!connectionManager.isConnected()) {
        error('There are currently no active connections to Cloudebug.');
        return;
    }
    const condition = await input({
        title: 'Enter the condition for your new breakpoint (leave blank to skip)'
    });
    if (typeof condition === 'undefined') {
        return;
    }
    const expressions: string[] = [];
    let expression = undefined;
    do {
        expression = await input({
            title: 'Enter an expression to evaluate at your new breakpoint (leave blank to skip)'
        });
        if (typeof expression === 'undefined') {
            // User cancelled action.
            return;
        }
        if (expression !== '') {
            expressions.push(expression);
        }
    } while (expression !== '');
    const formattedCondition = condition === '' ? undefined : condition;
    connectionManager.send({
        condition: formattedCondition,
        expressions,
        file: fileName,
        line: getCurrentLine(),
        type: 'add'
    });
    await connectionManager.waitForEvent<BreakpointData[]>('add');
    updateDecorationsFunc();
}

async function removeCmd(updateDecorationsFunc: () => void,
    node: BreakpointTreeItem | BreakpointData) {
    if (!connectionManager.isConnected()) {
        error('There are currently no active connections to Cloudebug.');
        return;
    }
    const breakpointId = (node instanceof BreakpointTreeItem) ?
        node.data.id :
        node.id;
    connectionManager.send({
        type: 'remove',
        id: breakpointId
    });
    await connectionManager.waitForEvent<number>('remove');
    updateDecorationsFunc();
};

async function hitsCmd(breakpointsTreeView: vscode.TreeView<vscode.TreeItem>) {
    const breakpointOnLine = getBreakpointOnLine();
    if (!breakpointOnLine) {
        error('There is no Cloudebug breakpoint on the specified line.');
        return;
    }
    const breakpointTreeItems = breakpointsTree.getChildren();
    if (!Array.isArray(breakpointTreeItems)) {
        return;
    }
    const breakpointToFocus = breakpointTreeItems.find(bti =>
        bti instanceof BreakpointTreeItem &&
        bti.data.id === breakpointOnLine.id
    );
    if (breakpointToFocus) {
        await breakpointsTreeView.reveal(breakpointToFocus, {
            expand: true,
            focus: true,
            select: true
        });
    }
};

async function addRemoveCmd() {
    const breakpointOnLine = getBreakpointOnLine();
    if (breakpointOnLine) {
        cmd.executeCommand('cloudebug.remove', breakpointOnLine);
    } else {
        cmd.executeCommand('cloudebug.add');
    }
}

export function activate(context: vscode.ExtensionContext) {
    const breakpointDecoration = win.createTextEditorDecorationType({
        gutterIconPath: context.asAbsolutePath('resources/breakpoint.svg')
    });
    const updateDecorationsWithIcon = updateDecorations
        .bind(null, breakpointDecoration);
    updateDecorationsWithIcon();
    win.onDidChangeActiveTextEditor(
        updateDecorationsWithIcon,
        null,
        context.subscriptions
    );
    vscode.workspace.onDidChangeTextDocument(
        updateDecorationsWithIcon,
        null,
        context.subscriptions
    );
    const breakpointsTreeView = win.createTreeView('cloudebug.breakpoints', {
        treeDataProvider: breakpointsTree,
        showCollapseAll: true
    });
    const addCmdBound = addCmd.bind(null, updateDecorationsWithIcon);
    const removeCmdBound = removeCmd.bind(null, updateDecorationsWithIcon);
    const hitsCmdBound = hitsCmd.bind(null, breakpointsTreeView);
    context.subscriptions.push(
        win.registerTreeDataProvider('cloudebug.breakpoints', breakpointsTree),
        cmd.registerCommand('cloudebug.connect', connectCmd),
        cmd.registerCommand('cloudebug.add', addCmdBound),
        cmd.registerCommand('cloudebug.remove', removeCmdBound),
        cmd.registerCommand('cloudebug.addremove', addRemoveCmd),
        cmd.registerCommand('cloudebug.hits', hitsCmdBound)
    );
    connectionManager.on('disconnected', function(expected: boolean) {
        if (!expected) {
            error('Cloudebug server unexpectedly closed the connection.');
        }
        serverData.clear();
        updateDecorationsWithIcon();
    }).on('error', (error: Error) => {
        win.showErrorMessage(error.message);
    }).on('add', (breakpoints: BreakpointData[]) => {
        serverData.addBreakpoints(breakpoints);
        updateDecorationsWithIcon();
    }).on('remove', (breakpointId: number) => {
        serverData.removeBreakpoints([breakpointId]);
        updateDecorationsWithIcon();
    }).on('hit', (hits: HitEvent[]) => {
        serverData.addHits(hits);
    });
}

export function deactivate() {
    connectionManager.disconnect();
}
