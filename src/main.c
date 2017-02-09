/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "util.h"
#include "cluster.h"
#include "commands.h"
#include "networking.h"


static void *form_cluster_thread(void *p) {

    int len;
    sleep(2);

    while (instance.lock == 1) {

        list_node *cursor = instance.cluster->head;

        while(cursor) {
            /* count for UNREACHABLE nodes */
            len = cluster_unreachable_count();
            cluster_node *n = (cluster_node *) cursor->data;
            if (cluster_reachable(n) == 0) {
                LOG(DEBUG, "Trying to connect to cluster node %s:%d\n",
                        n->addr, n->port);
                char p[5];
                sprintf(p, "%d", n->port);
                if (cluster_join(n->addr, p) == 1) len--;
            } else {
                if (n->self == 0) len--;
            }
            cursor = cursor->next;
        }

        if (len <= 0) instance.lock = 0; // all nodes are connected, release lock

        LOG(DEBUG, "Instance lock: %u\n", instance.lock);
        sleep(3);
    }

    LOG(DEBUG, "Cluster node succesfully joined to the cluster\n");
    cluster_balance();
    LOG(DEBUG, "All cluster nodes are balanced\n");
    // debug check
    // FIXME: remove
    list_node *cursor = instance.cluster->head;
    while(cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        LOG(DEBUG, "Node: %s:%d - Min: %u Max: %u Name: %s Fd: %d\n",
                n->addr, n->port, n->range_min, n->range_max, n->name, n->fd);
        cursor = cursor->next;
    }
    return NULL;
}


int main(int argc, char **argv) {

    /* Initialize random seed */
    srand((unsigned int) time(NULL));
    char *address = "127.0.0.1";
    char *port = "6373";
    char *filename = "./conf/cluster.conf";
    char *id = "A";
    int opt, cluster_mode = 0;
    static pthread_t thread;

    while((opt = getopt(argc, argv, "a:i:p:cf:")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'i':
                id = optarg;
                break;
            case 'c':
                cluster_mode = 1;
                break;
            case 'f':
                filename = optarg;
                break;
            default:
                cluster_mode = 0;
                break;
        }
    }

    char bus_port[20];
    int bport = GETINT(port) + 100;
    sprintf(bus_port, "%d", bport);

    /* initialize two sockets:
     * - one for incoming client connections
     * - a second for intercommunication between nodes
     */
    int sockets[2] = {
        listento(address, port),
        listento(address, bus_port)
    };


    /* handler function for incoming data */
    fd_handler handler_ptr = &command_handler;

    /* If cluster mode is enabled Initialize cluster map */
    if (cluster_mode == 1) {

        cluster_init(1, id, address, bus_port);

        /* read cluster configuration */
        FILE *file = fopen(filename, "r");
        char line[256];
        int linenr = 0;

        while (fgets(line, 256, (FILE *) file) != NULL) {

            char *ip = malloc(15), *pt = malloc(5), *name = malloc(256);
            linenr++;

            /* skip comments line */
            if (line[0] == '#') continue;

            if (sscanf(line, "%s %s %s", ip, pt, name) != 3) {
                fprintf(stderr, "Syntax error, line %d\n", linenr);
                continue;
            }

            LOG(DEBUG, "[CFG] Line %d: IP %s PORT %s NAME %s\n", linenr, ip, pt, name);

            /* create a new node and add it to the list */
            cluster_node *new_node =
                (cluster_node *) shb_malloc(sizeof(cluster_node));
            new_node->addr = ip;
            new_node->port = GETINT(pt);
            new_node->state = UNREACHABLE;
            new_node->self = 0;
            new_node->name = name;

            /* add the node to the cluster list */
            cluster_add_node(new_node);

        }

        /* start forming the thread */
        if (pthread_create(&thread, NULL, &form_cluster_thread, NULL) != 0)
            perror("ERROR pthread");

        /* pthread_join(thread, NULL); */

    } else {

        /* single node mode */
        cluster_init(0, id, address, bus_port);
    }

    /* start the main listen loop */
    event_loop(sockets, 2, handler_ptr);

    return 0;
}
