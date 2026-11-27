Put the tour's MP3 recordings in this folder.

 * Name them with a number first so they play in order:
       01_Transportation_Centre.mp3
       02_WAC_Bennett.mp3
   The list shows "Transportation Centre", "WAC Bennett", ... (number,
   extension and underscores are dropped).

 * Directions to a stop (from the stop before it) go in a .txt file with
   the same name, e.g. 02_WAC_Bennett.txt. Without a file the screen shows
   "temp directions to location N". Stop 1 has no directions.
   Blank lines start a new paragraph. About 250 characters fit in large text.

 * The folder is copied to the board by scripts/deploy.sh.
