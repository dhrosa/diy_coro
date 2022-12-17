#! /bin/bash

find .. -type f | grep -v ../build/ | entr -c -r sh -c 'cmake --build . && ctest --output-on-failure --progress'
 
