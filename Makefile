# Nikhil Nayyar
# CMPUT 275 Assignment P2
# 1614962
all: server client 

server: server/server.o server/digraph.o server/digraph.h server/dijkstra.o server/dijkstra.h
	g++ -o server/server server/server.o server/digraph.o server/dijkstra.o

server.o: server/server.cpp
	g++ -c server/server.cpp

dijkstra.o: server/dijkstra.cpp server/dijkstra.h
	g++ -c server/dijkstra.cpp

digraph.o: digraph.cpp digraph.h
	g++ -c server/digraph.cpp

client: client/client.o
	g++ -o client/client client/client.o

client.o: client/client.cpp
	g++ client/client.cpp

clean:
	unlink inpipe 
	unlink outpipe
	rm -f server/server server/server.o server/digraph.o server/dijkstra.o client/client.o client/client