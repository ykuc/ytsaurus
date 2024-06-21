GO_LIBRARY()

LICENSE(Apache-2.0)

SRCS(
    build_cancel.go
    build_prune.go
    checkpoint_create.go
    checkpoint_delete.go
    checkpoint_list.go
    client.go
    client_deprecated.go
    config_create.go
    config_inspect.go
    config_list.go
    config_remove.go
    config_update.go
    container_attach.go
    container_commit.go
    container_copy.go
    container_create.go
    container_diff.go
    container_exec.go
    container_export.go
    container_inspect.go
    container_kill.go
    container_list.go
    container_logs.go
    container_pause.go
    container_prune.go
    container_remove.go
    container_rename.go
    container_resize.go
    container_restart.go
    container_start.go
    container_stats.go
    container_stop.go
    container_top.go
    container_unpause.go
    container_update.go
    container_wait.go
    disk_usage.go
    distribution_inspect.go
    envvars.go
    errors.go
    events.go
    hijack.go
    image_build.go
    image_create.go
    image_history.go
    image_import.go
    image_inspect.go
    image_list.go
    image_load.go
    image_prune.go
    image_pull.go
    image_push.go
    image_remove.go
    image_save.go
    image_search.go
    image_tag.go
    info.go
    interface.go
    interface_experimental.go
    interface_stable.go
    login.go
    network_connect.go
    network_create.go
    network_disconnect.go
    network_inspect.go
    network_list.go
    network_prune.go
    network_remove.go
    node_inspect.go
    node_list.go
    node_remove.go
    node_update.go
    options.go
    ping.go
    plugin_create.go
    plugin_disable.go
    plugin_enable.go
    plugin_inspect.go
    plugin_install.go
    plugin_list.go
    plugin_push.go
    plugin_remove.go
    plugin_set.go
    plugin_upgrade.go
    request.go
    secret_create.go
    secret_inspect.go
    secret_list.go
    secret_remove.go
    secret_update.go
    service_create.go
    service_inspect.go
    service_list.go
    service_logs.go
    service_remove.go
    service_update.go
    swarm_get_unlock_key.go
    swarm_init.go
    swarm_inspect.go
    swarm_join.go
    swarm_leave.go
    swarm_unlock.go
    swarm_update.go
    task_inspect.go
    task_list.go
    task_logs.go
    utils.go
    version.go
    volume_create.go
    volume_inspect.go
    volume_list.go
    volume_prune.go
    volume_remove.go
    volume_update.go
)

IF (OS_LINUX)
    SRCS(
        client_unix.go
    )
ENDIF()

IF (OS_DARWIN)
    SRCS(
        client_unix.go
    )
ENDIF()

IF (OS_WINDOWS)
    SRCS(
        client_windows.go
    )
ENDIF()

END()

RECURSE(
    buildkit
)
