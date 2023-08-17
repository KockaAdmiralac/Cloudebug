from http import HTTPStatus
from flask import Flask, Response, jsonify, request
from cloudebug import init

app = Flask(__name__)

state = 0

def inc_state() -> int:
    global state
    state += 1
    return state

@app.route('/', methods=['GET'])
def index():
    return jsonify(version='1.0.0', state=state)

@app.route('/order', methods=['POST'])
def order():
    if int(request.args.get('a', 123)) % 2 == 0:
        for i in range(10):
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
            print('A')
    else:
        print('B')
    print('C')
    return Response(status=HTTPStatus.CREATED)

@app.route('/search', methods=['GET'])
def search():
    return jsonify(orders=[
        {
            'id': 1,
            'quantity': 10
        }
    ])

def _main():
    app.run('0.0.0.0', 5000)

if __name__ == '__main__':
    init(_main)
