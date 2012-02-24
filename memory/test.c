#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <lib.h>
#include <minix/ipc.h>
#include </usr/src/include/minix/type.h>
#include <stdlib.h>
#define NIL_HOLE (struct hole *) 0

int sort(const void *x, const void *y){
  return(*(int*)x - *(int*)y);
}

int main(){
  char alg_type = '0';

  int algo_type = 0;
  int num_holes = 0;
  int num = 0;
  int length = 0;
  int itor = 0;
  int median = 0;
  int *array;

  float total = 0;
  float avg_hole = 0;
  float med_hole = 0;
  float stdev_curr = 0;
  float stdev_total = 0;
  float standard_dev = 0;

  struct pm_mem_info pmi;
  struct hole *hp;

  printf("Enter a number between 0 and 4\n  First fit:0\n  Next fit:1\n  Random fit:2\n  Worst fit:3\n  Best fit:4\n");
  alg_type = getchar();
  algo_type = alg_type - 48;
  array = malloc(64*sizeof(int) );
  memcall(algo_type);

  while(1){
  num_holes = 0;
  length = 0;
  itor = 0;
  median = 0;
  total = 0;
  avg_hole = 0;
  med_hole = 0;
  stdev_curr = 0;
  stdev_total = 0;
  standard_dev = 0;
  getsysinfo(MM, SI_MEM_ALLOC, &pmi);
  num_holes = sizeof(pmi.pmi_holes[itor]);

  for(itor = 0; itor < num_holes-1;++itor){
    length = pmi.pmi_holes[itor].h_len;
    if(length != 0){
      total += length;
      array[itor] = length;
      printf("Length = %d\n", length);
    }
  }
  avg_hole = total/num_holes;
  for(itor = 0; itor < num_holes-1;++itor){
    length = pmi.pmi_holes[itor].h_len;
    if(length != 0){
      stdev_curr = (length-avg_hole)*(length-avg_hole);
      stdev_total += stdev_curr;
    }
  }
  qsort(array, num_holes, sizeof(int), sort);
  for(itor = 0; itor < (num_holes)/2;itor++){
    median = array[itor];
  }
  standard_dev = sqrt( (stdev_total)/(num_holes-1) );
  printf("The number of holes %d\nThe average hole is %f\nThe standard deviation is %f\nThe median is %d\n", num_holes, avg_hole, standard_dev, median);
  sleep(1);
  }


  return 0;
}
