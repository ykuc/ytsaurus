RECURSE(
    chaos_cells
    bundle_hotfix
    clock_quorum_health
    controller_agent_alerts
    controller_agent_count
    controller_agent_operation_memory_consumption
    controller_agent_uptime
    destroyed_replicas_size
    discovery
    dynamic_table_commands
    dynamic_table_replication
    lost_vital_chunks
    map_result
    master
    master_alerts
    master_chunk_management
    medium_balancer_alerts
    missing_part_chunks
    oauth_health
    operations_archive_tablet_store_preload
    operations_count
    operations_satisfaction
    operations_snapshots
    proxy
    query_tracker_alerts
    query_tracker_chyt_liveness
    query_tracker_ql_liveness
    query_tracker_yql_liveness
    queue_agent_alerts
    queue_api
    quorum_health
    quorum_health
    register_watcher
    scheduler
    scheduler_alerts
    scheduler_alerts_jobs_archivation
    scheduler_alerts_update_fair_share
    scheduler_uptime
    sort_result
    stuck_missing_part_chunks
    suspicious_jobs
    system_quotas
    system_quotas_yt_logs
    tablet_cell_gossip
    tablet_cell_snapshots
    tablet_cells
    unaware_nodes
    wrapper_node_count
)

IF (NOT OPENSOURCE)
    INCLUDE(ya_non_opensource.inc)
ENDIF()
