# Type one of either 'make client' or 
# 'make server' to compile an executable. 
#
# Type 'make clean' to clear out the 
# executables.
#

client:
	gcc -o solitaire_c solitaire_c.c -std=c99 -lpthread 

server:
	gcc -o solitaire_s solitaire_s.c carddeck.c cardstack.c -std=c99 -lpthread

clean:
	rm -f solitaire_s solitaire_c *~
