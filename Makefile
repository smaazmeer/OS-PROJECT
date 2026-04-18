CC = gcc
CFLAGS = -Wall -pthread `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0`

OBJ = src/main.o src/stock.o src/account.o src/order_queue.o src/trading.o src/ui.o

stock_trading: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o stock_trading