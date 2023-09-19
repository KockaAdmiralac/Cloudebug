#!/bin/bash
wrk -t8 -c400 -d30s http://localhost:5000/?a=122
