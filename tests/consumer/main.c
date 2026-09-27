#include <moenotes_score.h>
#include <stdio.h>
int main(void) {
    printf("%s\n", mns_version());
    return mns_prepare(0, 0, 0, 0, 0, 0, 0, 0) != MNS_INVALID_INPUT;
}
