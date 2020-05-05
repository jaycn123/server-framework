#include "../BootServer/global.h"
#include "mq_cluster.h"
#include <string.h>

typedef struct ClusterTableItem_t {
	HashtableNode_t m_htnode;
	List_t clusterlist;
	size_t clusterlistcnt;
} ClusterTableItem_t;

List_t g_ClusterList;
Hashtable_t g_ClusterTable;
static HashtableNode_t* s_ClusterBulk[32];
static int __keycmp(const void* node_key, const void* key) { return strcmp((const char*)node_key, (const char*)key); }
static unsigned int __keyhash(const void* key) { return hashBKDR((const char*)key); }

int initClusterTable(void) {
	hashtableInit(&g_ClusterTable, s_ClusterBulk, sizeof(s_ClusterBulk) / sizeof(s_ClusterBulk[0]), __keycmp, __keyhash);
	listInit(&g_ClusterList);
	return 1;
}

Cluster_t* getCluster(const char* name, const IPString_t ip, unsigned short port) {
	HashtableNode_t* htnode = hashtableSearchKey(&g_ClusterTable, name);
	if (htnode) {
		ListNode_t* cur;
		ClusterTableItem_t* item = pod_container_of(htnode, ClusterTableItem_t, m_htnode);
		for (cur = item->clusterlist.head; cur; cur = cur->next) {
			Cluster_t* exist_cluster = pod_container_of(cur, Cluster_t, m_reg_htlistnode);
			if (!strcmp(exist_cluster->ip, ip) && exist_cluster->port == port) {
				return exist_cluster;
			}
		}
	}
	return NULL;
}

int regCluster(const char* name, Cluster_t* cluster) {
	ClusterTableItem_t* item;
	HashtableNode_t* htnode;
	if (cluster->session.has_reg) {
		return 1;
	}
	htnode = hashtableSearchKey(&g_ClusterTable, name);
	if (htnode) {
		item = pod_container_of(htnode, ClusterTableItem_t, m_htnode);
	}
	else {
		item = (ClusterTableItem_t*)malloc(sizeof(ClusterTableItem_t));
		if (!item)
			return 0;
		item->m_htnode.key = strdup(name);
		if (!item->m_htnode.key)
			return 0;
		item->clusterlistcnt = 0;
		listInit(&item->clusterlist);
		hashtableInsertNode(&g_ClusterTable, &item->m_htnode);
	}
	cluster->name = (const char*)item->m_htnode.key;
	cluster->m_reg_item = item;
	item->clusterlistcnt++;
	listPushNodeBack(&item->clusterlist, &cluster->m_reg_htlistnode);
	listPushNodeBack(&g_ClusterList, &cluster->m_reg_listnode);
	cluster->session.has_reg = 1;
	return 1;
}

void unregCluster(Cluster_t* cluster) {
	if (cluster->session.has_reg) {
		ClusterTableItem_t* item = (ClusterTableItem_t*)cluster->m_reg_item;
		listRemoveNode(&item->clusterlist, &cluster->m_reg_htlistnode);
		item->clusterlistcnt--;
		if (!item->clusterlist.head) {
			hashtableRemoveNode(&g_ClusterTable, &item->m_htnode);
			free((void*)item->m_htnode.key);
			free(item);
		}
		listRemoveNode(&g_ClusterList, &cluster->m_reg_listnode);
		cluster->session.has_reg = 0;
	}
}

void freeClusterTable(void) {
	HashtableNode_t* curhtnode, *nexthtnode;
	for (curhtnode = hashtableFirstNode(&g_ClusterTable); curhtnode; curhtnode = nexthtnode) {
		ListNode_t* curlistnode, *nextlistnode;
		ClusterTableItem_t* item = pod_container_of(curhtnode, ClusterTableItem_t, m_htnode);
		nexthtnode = hashtableNextNode(curhtnode);
		for (curlistnode = item->clusterlist.head; curlistnode; curlistnode = nextlistnode) {
			Cluster_t* cluster = pod_container_of(curlistnode, Cluster_t, m_reg_htlistnode);
			nextlistnode = curlistnode->next;
			ptr_g_SessionAction()->unreg(&cluster->session);
		}
		free((void*)item->m_htnode.key);
		free(item);
	}
	hashtableInit(&g_ClusterTable, s_ClusterBulk, sizeof(s_ClusterBulk) / sizeof(s_ClusterBulk[0]), NULL, NULL);
	listInit(&g_ClusterList);
}

Session_t* newSession(int type) {
	if (CHANNEL_TYPE_INNER == type) {
		Cluster_t* cluster = (Cluster_t*)malloc(sizeof(Cluster_t));
		if (cluster) {
			initSession(&cluster->session);
			cluster->session.usertype = type;
			//cluster->session.persist = 1;
			return &cluster->session;
		}
	}
	else if (CHANNEL_TYPE_HTTP == type) {
		Session_t* session = (Session_t*)malloc(sizeof(Session_t));
		if (session) {
			initSession(session);
			session->usertype = type;
			return session;
		}
	}
	return NULL;
}

void freeSession(Session_t* session) {
	if (CHANNEL_TYPE_INNER == session->usertype) {
		Cluster_t* cluster = pod_container_of(session, Cluster_t, session);
		unregCluster(cluster);
		free(cluster);
	}
	else if (CHANNEL_TYPE_HTTP == session->usertype) {
		free(session);
	}
}