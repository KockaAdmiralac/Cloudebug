from http import HTTPStatus
from flask import Flask, Response, jsonify, request
from cloudebug import init

app = Flask(__name__)

@app.route('/', methods=['GET'])
def index():
    return jsonify(version='1.0.0')

@app.route('/order', methods=['POST'])
def order():
    for i in range(10):
        print(i)
        print(i)
        print(i)
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
