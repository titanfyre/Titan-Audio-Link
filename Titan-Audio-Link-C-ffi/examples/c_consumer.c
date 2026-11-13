#include "TitanAudioLinkJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(titan_audio_link_json_open(&session))return 1;char *reply=titan_audio_link_json_call(session,"{",1);if(!reply){titan_audio_link_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;titan_audio_link_json_free(reply);if(titan_audio_link_json_close(session))return 3;return failed;}
