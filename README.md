# Cloudebug
This repository contains my graduation work, an exploration of building a debugger for production services running in the cloud. The repository is split into several parts:

- `code/package/cloudebug_helper`: CPython extension that manipulates bytecode for inserting and removing non-breaking breakpoints
- `code/package/cloudebug`: Wrapper Python code around the CPython extension that interfaces with the app being debugged and the developer connecting through a debugging client
- `code/vscode`: Visual Studio Code extension that serves as a debugging client
- `code/dummy-app`: A sample Flask app to test debugging on
- `report`: Report from my graduation work (in Serbian!)
