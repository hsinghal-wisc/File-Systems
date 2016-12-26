#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int
main(int argc, char *argv[]) {
  int fd;
  struct stat status;
  
  if(argc != 2){
    printf(1, "filestat pathname\n");
    exit();
  }

  if((fd = open(argv[1], 0)) < 0) {
    printf(2, "filestat: cannot open %s\n", argv[1]);
    exit();
  }
  if(fstat(fd, &status) < 0) {
    printf(2, "filestat: cannot stat %s\n", argv[1]);
    close(fd);
    exit();
  }

  switch(status.type) {
	case T_FILE:
    case T_DIR:
    case T_DEV:
      printf(1, "Type: %d\nSize: %d\n", status.type, status.size);
      break;
    case T_CHECKED:
      printf(1, "Type: %d\nSize: %d\nChecksum: %d\n", status.type, status.size, status.checksum);
      break;
  }
  close(fd);
  exit();
}
