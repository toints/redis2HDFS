/*
 * Author       : chenzutao
 * Date         : 2016-05-13
 * Function     : read data from redis and format it into json, then write into HDFS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "hdfs.h"
#include "cJSON.h"
#include "redis_operation.h"
#include "read_cfg.h"

#define OK 0
#define ERR -1

#define MAX_PATH_LEN 128

int main(int argc, char **argv)
{
    char cur_path[MAX_PATH_LEN] = { 0 };
    getcwd(cur_path, MAX_PATH_LEN);
    char conf_path[MAX_PATH_LEN + 32] = { 0 };

    sprintf(conf_path, "%s/config.json", cur_path);
    cfg_file_t *cfg_file = NULL;

    read_cfg(&cfg_file, conf_path);

    signal(SIGPIPE, SIG_IGN);

    redisContext *conn = redis_connect(cfg_file->redis_host, cfg_file->redis_port);
    if(!conn)
    {
        logger(E, PATH, "[** FAILED **] Failed to connect to redis [%s:%d].\n", cfg_file->redis_host, cfg_file->redis_port);
        return ERR;
    }

    hdfsFS fs = hdfsConnect("default", 0);
    //hdfsFS fs = hdfsConnect(cfg_file->hdfs_host, cfg_file->hdfs_port);
    if(!fs)
    {
        logger(E, PATH, "[** FAILED **] Failed to connect to HDFS [%s:%d].\n", cfg_file->hdfs_host, cfg_file->hdfs_port);
        return ERR;
    }

    hdfsFile write_file = hdfsOpenFile(fs, cfg_file->hdfs_file, O_WRONLY | O_CREAT, 4096, 0, 0);
    if(!write_file)
    {
        logger(E, PATH, "[** FAILED **] Failed to open file .\n");
        return ERR;
    }

    int cnt = 10;
    while(cnt--)
    {
    	redisReply *lrange_key = redis_lrange(conn, cfg_file->redis_zset_key, cfg_file->zset_min_index, cfg_file->zset_max_index);
    	char key[512] = { 0 }; //FIXME
    	if(lrange_key && lrange_key->type == REDIS_REPLY_ARRAY)
    	{
        	if(lrange_key->elements > 0 && lrange_key->element[0]->str)
        	{
            		strcpy(key, lrange_key->element[0]->str);
        	}
        	else
        	{
            		logger(E, PATH, "lrange [%s] result is empty.\n", cfg_file->redis_zset_key);
            		continue;
        	}
    	}
    	else
    	{
		if(!lrange_key) { logger(E, PATH, "lrange key is null .\n"); return ERR;}
		logger(D, PATH, "lrange key type:%d\n", lrange_key->type);
		
        	fprintf(stderr, "execute lrange command failed .\n");
		return ERR;
        	//continue;
    	}

    	freeReplyObject(lrange_key);
    	lrange_key = NULL;

	logger(D, PATH, "key :%s\n", key);
    	redisReply *reply = redis_hgetall(conn, key);
    	if(!reply)
   	{
        	logger(E, PATH, "failed to execute [HGETALL %s].\n", key);
		continue;
    	}
	printf("reply type:%d\n", reply->type);
/*TODO
    	int del = redis_delete(conn, key);
    	if(del)
    	{
        	logger(E, PATH, "failed to delete [%s] from redis.\n", key);
    	}
*/
    	int i = 0;
    	//FIXME : buffer format or cJSON create ?

    	if(reply->type == REDIS_REPLY_ARRAY)
    	{
	    	cJSON *head_json = cJSON_CreateObject();
    		if(!head_json)
    		{
        		logger(E, PATH, "failed to create json object.\n");
        		freeReplyObject(reply);
			continue;
    		}

		logger(D, PATH, "elements:%ld\n", reply->elements);
        	for(i=0; i<reply->elements; i+=2)
        	{
			logger(D, PATH, "%s : %s\n", reply->element[i]->str, reply->element[i+1]->str);
            		cJSON_AddStringToObject(head_json, reply->element[i]->str, reply->element[i+1]->str);
        	}
	    	char *head_str = cJSON_PrintUnformatted(head_json);
    		logger(D, PATH, "%s\n", head_str);
    		if(!head_str)
    		{
        		logger(E, PATH, "failed to format into json .\n");
        		freeReplyObject(reply);
			cJSON_Delete(head_json);
			continue;
    		}

    		hdfsWrite(fs, write_file, head_str, strlen(head_str));
    		hdfsWrite(fs, write_file, "\n", 1);

    		cJSON_Delete(head_json);
    	}

    	freeReplyObject(reply);
    }

    hdfsCloseFile(fs, write_file);
    hdfsDisconnect(fs);
    redisFree(conn);

    return OK;
}

