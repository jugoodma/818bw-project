#!/bin/bash

move () {
    for i in {1..4}
    do
        curl -X POST -H "Content-Type: text/plain" --data "r,90" http://$1/mov > /dev/null 2>&1
        curl -X POST -H "Content-Type: text/plain" --data "f,5" http://$1/mov > /dev/null 2>&1
    done
    for i in {1..4}
    do
        curl -X POST -H "Content-Type: text/plain" --data "r,-90" http://$1/mov > /dev/null 2>&1
        curl -X POST -H "Content-Type: text/plain" --data "b,5" http://$1/mov > /dev/null 2>&1
    done
}

start=$SECONDS

cleanup ()
{
    kill -s SIGTERM $!
    duration=$(( SECONDS - start ))
    echo $duration
    exit 0
}

trap cleanup SIGINT SIGTERM

while [ 1 ]
do
    move &
    wait $!
done
