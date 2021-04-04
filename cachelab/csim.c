#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include "cachelab.h"

unsigned long s, E, b;

unsigned long get_tag(unsigned long address)
{
    return address >> (s + b);
}

unsigned long get_set(unsigned long address)
{
    return (address >> b) - (get_tag(address) << s);
}

struct cache_line
{
    int valid_bit;
    int time;
    unsigned long tag;
};

int load(struct cache_line *cache, unsigned long tag, int time)
{
    for (unsigned long i = 0; i < E; ++i)
    {
        struct cache_line line = cache[i];
        if (line.valid_bit && line.tag == tag)
        {
            cache[i].time = time;
            return 1;
        }
    }
    return 0;
}

int overwrite(struct cache_line *cache, unsigned long tag, int time)
{
    int min_time = 0x7fffffff;
    unsigned long min_index = -1, index = -1;
    for (unsigned long i = 0; i < E; ++i)
    {
        struct cache_line line = cache[i];
        if (!line.valid_bit)
        {
            index = i;
            break;
        }
        else if (line.time < min_time)
        {
            min_time = line.time;
            min_index = i;
        }
    }
    if (index == -1)
        index = min_index;

    cache[index].valid_bit = 1;
    cache[index].time = time;
    cache[index].tag = tag;

    return index == min_index;
}

int main(int argc, char **argv)
{
    int opt, verbose_flag;
    char *trace;
    /* Loop over arguments */
    while ((opt = getopt(argc, argv, "vs:E:b:t:")) != -1)
    {
        /* Determine which argument it's processing */
        switch (opt)
        {
            case 'v':
                verbose_flag = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                trace = optarg;
                break;
            default:
                exit(-1);
                break;
        }
    }

    struct cache_line cache[1 << s][E];
    for (int i = 0; i < (1 << s); ++i)
        for (int j = 0; j < E; ++j)
            cache[i][j].valid_bit = 0;

    /* Open file for reading */
    FILE *pFile = fopen(trace, "r");
    char identifier;
    unsigned long address;
    int size;

    int time = 0, hits = 0, misses = 0, evictions = 0;
    unsigned long set, tag;

    /* Read lines like " M 20,1" or " L 19,3" */
    while (fscanf(pFile, " %c %lx,%d", &identifier, &address, &size) > 0)
    {
        if (identifier == 'I')
            continue;
        
        if (verbose_flag)
            printf("%c %lx,%d", identifier, address, size);

        set = get_set(address);
        tag = get_tag(address);

        if (load(cache[set], tag, time))
        {
            ++hits;
            if (verbose_flag)
                printf(" hit");
        }
        else
        {
            ++misses;
            if (verbose_flag)
                printf(" miss");

            if (overwrite(cache[set], tag, time))
            {
                ++evictions;
                if (verbose_flag)
                    printf(" eviction");
            }
        }

        if (identifier == 'M')
        {
            ++hits;
            if (verbose_flag)
                printf(" hit");
        }

        if (verbose_flag)
            printf("\n");
        ++time;
    }

    printSummary(hits, misses, evictions);
    return 0;
}
