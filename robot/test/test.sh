#!/bin/bash

p () {
    read -n 1 -s -r -p "done?"
}

echo "ensure the server is on!"
# docker run -it --rm -p 42:42 818b

# do beep
curl -X POST -H "Content-Type: text/plain" --data "440" http://$1/bep
p

# do ultrasonic
curl -X POST -H "Content-Type: text/plain" --data "10" http://$1/ult
p

# do mic sample
curl -X POST -H "Content-Type: text/plain" --data "l,500,500" http://$1/loc
p
curl -X POST -H "Content-Type: text/plain" --data "s,500,500" http://$1/loc
p

# do movement
curl -X POST -H "Content-Type: text/plain" --data "f,10" http://$1/mov
p
curl -X POST -H "Content-Type: text/plain" --data "b,10" http://$1/mov
p
curl -X POST -H "Content-Type: text/plain" --data "r,90" http://$1/mov
p
curl -X POST -H "Content-Type: text/plain" --data "r,-90" http://$1/mov
p

# todo accel
