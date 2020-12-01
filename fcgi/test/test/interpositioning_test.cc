#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[])
{
  if(puts("test") == EOF)
  {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
