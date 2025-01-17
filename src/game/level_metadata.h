#ifndef LEVEL_METADATA_H_
#define LEVEL_METADATA_H_

typedef struct LevelMetadata LevelMetadata;
typedef struct LineStream LineStream;

LevelMetadata *create_level_metadata(const char *title);
LevelMetadata *create_level_metadata_from_file(const char *filename);
LevelMetadata *create_level_metadata_from_line_stream(LineStream *line_stream);
void destroy_level_metadata(LevelMetadata *level_metadata);

const char *level_metadata_title(const LevelMetadata *level_metadata);

#endif  // LEVEL_METADATA_H_
