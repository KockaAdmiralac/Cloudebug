#!/bin/bash
wrk -t8 -c400 -d30s -s post.lua http://localhost:5000/order?a=122
