# DIRECTORY=./teste3

clear

# if [ -d "$DIRECTORY" ]; then
#     rm -r "$DIRECTORY"
# fi

gcc -std=c99 bpt.h bpt.c primarykey.c -o out

./out