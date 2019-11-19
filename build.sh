DIRECTORY=./teste3

clear

if [ -d "$DIRECTORY" ]; then
    rm -r "$DIRECTORY"
fi

gcc -std=c99 primarykey.c bpt.c -o out

./out