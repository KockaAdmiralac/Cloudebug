from setuptools import Extension, find_packages, setup

setup(
    name='cloudebug',
    author='Luka Simić',
    author_email='luka@kocka.tech',
    description='Cloud debugging tools for my graduation work.',
    license='MIT',
    packages=find_packages(include=['cloudebug']),
    ext_modules=[
        Extension(
            name='cloudebug_helper',
            sources=['cloudebug_helper/ext.cpp'],
        ),
    ],
    data_files=[
        ('cloudebug_helper', [
            'cloudebug_helper/py.typed',
            'cloudebug_helper/__init__.pyi'
        ])
    ],
    install_requires=['websockets']
)
