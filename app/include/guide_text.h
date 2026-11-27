// guide_text.h
// Directions to each tour stop, shown in the guide popup.
//
// The text for stop N lives in a .txt file with the same name as the stop's
// MP3, in the same folder:   03_Convo_and_DFA.mp3  ->  03_Convo_and_DFA.txt
// It describes how to get to stop N from stop N-1. Stop 1 has no directions.
// If the file is missing, the placeholder "temp directions to location N" is
// used, so adding or renaming recordings never needs a code change.
//
// The screen's built-in font is ISO 8859-1 (Latin-1), so UTF-8 text is
// converted: accented letters are kept, and "smart" quotes, dashes and the
// like (as typed by Word / Google Docs) become plain ASCII.

#ifndef _GUIDE_TEXT_H_
#define _GUIDE_TEXT_H_

#include <stdbool.h>
#include <stddef.h>

#define GUIDE_TEXT_MAX 1200   // longer files are cut

// index is 0-based (stop 1 = index 0). Returns false if the stop has no
// directions (the first stop, or an index out of range).
bool guide_text_get(int index, char *out, size_t out_size);

// UTF-8 -> Latin-1 conversion (exposed for the unit test). Also turns tabs
// into spaces, drops '\r', and trims leading/trailing whitespace.
void guide_text_to_latin1(const char *in, char *out, size_t out_size);

#endif
