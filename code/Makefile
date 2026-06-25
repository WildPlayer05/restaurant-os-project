CC = gcc
CFLAGS = -Wall -Wextra -pthread

build:
	$(CC) $(CFLAGS) -o restaurant main.c customer.c waiter.c cook.c rng.c -lm
	chmod +x bootstrap.sh status.sh

clean:
	rm -f restaurant /tmp/restaurant.pid

run: build
	./bootstrap.sh $(ARGS)
