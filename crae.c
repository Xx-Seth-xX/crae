#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define READ_END 0
#define WRITE_END 1

#define array_sizeof(arr) ((sizeof((arr))) / (sizeof((arr)[0])))

char buffer[1024 * 1024 * 20];

#define myalloc(nbytes) (_myalloc((nbytes), __FILE__, __LINE__))
void *_myalloc(size_t nbytes, const char *file, int line) {
  void *ptr = malloc(nbytes);
  if (ptr == NULL) {
    fprintf(stderr, "Error while allocating memory in file %s:%i\n", file,
            line);
    exit(1);
  }
  return ptr;
}

typedef struct {
  char *data;
  size_t len;
} SView;

typedef struct {
  SView content;
  size_t pos;
} Lexer;

typedef struct {
  SView content;
  size_t cap;
} SBuilder;

SView sv_from_str(char *str) {
  return (SView){
      .data = str,
      .len = strlen(str),
  };
}

SBuilder sb_with_cap(size_t cap) {
  SBuilder sb = {0};
  sb.cap = cap;
  sb.content.data = myalloc(sizeof(char) * cap);
  return sb;
}

SBuilder sb_from_sv(SView sv) {
  SBuilder sb = {};
  sb.cap = sv.len * 2;
  sb.content.data = myalloc(sizeof(char) * sb.cap);
  memcpy(sb.content.data, sv.data, sv.len);
  return sb;
}

void sb_append(SBuilder *sb, SView sv) {
  if (sv.len + sb->content.len >= (sb->cap - 1)) {
    size_t new_size = (sv.len + sb->content.len) * 2;
    sb->content.data = realloc(sb->content.data, new_size);
    if (sb->content.data == NULL) {
      fprintf(stderr, "Sin memoria\n");
      exit(1);
    }
    sb->cap = new_size;
  }
  memcpy(sb->content.data + sb->content.len, sv.data, sv.len);
  sb->content.len += sv.len;
}

const char *sb_to_owned_string(SBuilder sb) {
  char *str = myalloc(sb.content.len * sizeof(char));
  memcpy(str, sb.content.data, sb.content.len);
  return str;
}
const char *sb_to_string(SBuilder sb) {
  sb.content.data[sb.content.len] = '\0';
  char *str = sb.content.data;
  return str;
}

void sb_pushc(SBuilder *sb, char c) {
  if (sb->content.len > sb->cap - 1) {
    size_t new_size = (sb->content.len + 1) * 2;
    sb->content.data = realloc(sb->content.data, new_size);
    if (sb->content.data == NULL) {
      fprintf(stderr, "Sin memoria\n");
      exit(1);
    }
    sb->cap = new_size;
  }
  sb->content.data[sb->content.len] = c;
  ++sb->content.len;
  return;
}

size_t get_webpage(char *url) {
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    fprintf(stderr, "Couldn't create pipe due to %s", strerror(errno));
    exit(1);
  }
  pid_t child = fork();

  if (child == -1) {
    fprintf(stderr, "Couldn't create child\n");
    exit(1);
  }
  if (child == 0) {
    // Child process
    if (close(pipefd[READ_END]) == -1) {
      fprintf(stderr, "Couldn't close reading end of pipe due to %s",
              strerror(errno));
      exit(1);
    }
    if (dup2(pipefd[WRITE_END], STDOUT_FILENO) == -1) {
      fprintf(stderr, "Couldn't redirect stdout to pipe due to %s\n",
              strerror(errno));
      exit(1);
    }
    char *user_agent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
        "like Gecko) Chrome/109.0.0.0 Safari/537.36";

    (void)url;
    // printf("%s", user_agent);
    // printf("%s", url);
    execlp("wget", "wget", url, "-O", "-", "-U", user_agent, "--quiet", NULL);
    assert(0 && "unreachable");
  }
  // Parent process
  if (close(pipefd[WRITE_END]) == -1) {
    fprintf(stderr, "Couldn't close writing end of pipe due to %s",
            strerror(errno));
    exit(1);
  }

  size_t pointer = 0;
  while (true) {
    size_t n = read(pipefd[READ_END], buffer + pointer, array_sizeof(buffer));
    if (n <= 0) {
      break;
    }
    pointer += n;
    // printf("Received %li bytes\n", n);
    // printf("Received `%.*s`\n", (int)n, buffer);
  }
  return pointer;
}

char get_next_char(Lexer *l) {
  if (l->pos < l->content.len) {
    return l->content.data[l->pos++];
  }
  return 0;
}

int get_sv_surrounded_by(SView sv, SView *out_sv, const char *begin_str,
                         const char *end_str, size_t ind) {
  Lexer l = {0};
  l.content.data = &sv.data[ind];
  l.content.len = sv.len - ind;
  // const char *mstr = "<p class=\"j";
  // const char *endmstr = "</p>";
  // const char *mstr = "DOC";
  // const char *endmstr = "PE";
  size_t mind = 0;
  size_t origin = 0;
  // sv->data = l->content.data;
  char c = get_next_char(&l);
  while (c != 0) {
    if (c == begin_str[mind]) {
      ++mind;
      if (mind == strlen(begin_str)) {
        origin = l.pos - strlen(begin_str);
        // Now we have to look for ending
        mind = 0;
        c = get_next_char(&l);
        while (c != 0) {
          if (c == end_str[mind]) {
            ++mind;
            if (mind == strlen(end_str)) {
              out_sv->data = (l.content.data + origin);
              out_sv->len = l.pos - origin;
              return l.pos;
            }
          } else {
            mind = 0;
          }
          c = get_next_char(&l);
        }
        return -1;
      }
    } else {
      mind = 0;
    }
    c = get_next_char(&l);
  }
  return -1;
}

int get_next_definition(Lexer *l, SBuilder *sb) {
  // const char *mstr = "<p class=\"j";
  // const char *endmstr = "</p>";
  SView sv = l->content;
  SView whole_def = {0};
  static char buff[10 * 2048];
  int def_len =
      get_sv_surrounded_by(sv, &whole_def, "<p class=\"j", "</p>", l->pos);
  if (def_len == -1) {
    return -1;
  }
  int final_pos = l->pos + def_len;

  sb->content.len = 0;

  SView aux = {0};

  int n = get_sv_surrounded_by(whole_def, &aux, ">", "<", 0);
  int curr_pos = 0;
  while (n != -1 && curr_pos < def_len) {
    // printf("%.*s\n", (int) aux.len, aux.data);
    // printf("%i\n", (int) curr_pos);
    sb_append(sb, aux);
    n = get_sv_surrounded_by(whole_def, &aux, ">", "<", curr_pos);
    curr_pos += n;
  }
  const char *partially_parsed = sb_to_string(*sb);
  memcpy(buff, partially_parsed, sb->content.len + 1);
  sb->content.len = 0;
  for (size_t i = 0; i < strlen(partially_parsed); ++i) {
    char c = buff[i];
    if (c != '<' && c != '>') {
      sb_pushc(sb, c);
    }
  }
  partially_parsed = sb_to_string(*sb);
  memcpy(buff, partially_parsed, sb->content.len + 1);
  sb->content.len = 0;
  for (size_t i = 0; i < strlen(partially_parsed); ++i) {
    char c = buff[i];
    const char *sin = "Sin.:";
    const char *used = "U.";
    if (memcmp(buff + i, sin, strlen(sin)) == 0 ||
        memcmp(buff + i, used, strlen(used)) == 0) {
      sb_pushc(sb, '\n');
      sb_pushc(sb, '\t');
    }
    sb_pushc(sb, c);
  }

  l->pos = final_pos;
  return 0;
}

int main(void) {
  size_t nbread = get_webpage("https://dle.rae.es/perro");
  Lexer l = {0};
  l.pos = 0;
  l.content = (SView){
      .data = buffer,
      .len = nbread,
  };
  SBuilder sb = sb_with_cap(10 * 1024);

  int counter = 1;
  while (get_next_definition(&l, &sb) != -1) {
    // printf("%.*s", (int)sv.len, sv.data);
    printf("Acepción:\n\t%s\n", sb_to_string(sb));
    ++counter;
  }

  free(sb.content.data);
  return 0;
}
