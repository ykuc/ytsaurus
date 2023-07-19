import sys
from yt_env_setup import YTEnvSetup

from yt_commands import (
    authors, get, set,
    create_user, make_ace,
    create_access_control_object_namespace, create_access_control_object,
    execute_command, get_active_primary_master_follower_address)

from yt.common import YtResponseError

import pytest

##################################################################


class TestAdminCommandsACL(YTEnvSetup):
    NUM_MASTERS = 2

    def setup_class(cls):
        super(TestAdminCommandsACL, cls).setup_class()

    def _check_raises_superuser_permissions_required(self, func):
        def wrapper_func(*args, **kwargs):
            result = None
            with pytest.raises(YtResponseError):
                try:
                    result = func(*args, **kwargs)
                except YtResponseError as e:
                    assert e is not None
                    print(f"No superusers role raises error: {str(e)}", file=sys.stderr)
                    assert "Superuser permissions required" in str(e)
                    raise e
            return result
        return wrapper_func

    def _create_and_update_aco(self, command: str, user: str, action="allow", permissions="use", namespace="admin_commands"):
        try:
            create_access_control_object_namespace(namespace)
        except YtResponseError as e:
            assert f'Access control object namespace "{namespace}" already exists' in str(e)

        try:
            create_access_control_object(command, namespace)
        except YtResponseError as e:
            assert f'Access control object "{namespace}"/"{command}" already exists' in str(e)

        set(f"//sys/access_control_object_namespaces/{namespace}/{command}/@principal_acl/end",
            make_ace(action, user, permissions))

    @authors("ni-stoiko")
    def test_acl_switch_leader(self):
        """
        Test for separation ACO switch_leader from superusers.
        """
        command = "switch_leader"
        user = "u1"

        def _switch_leader(cell_id, new_leader_address, authenticated_user=None):
            parameters = {"cell_id": cell_id, "new_leader_address": new_leader_address, "authenticated_user": authenticated_user}
            return execute_command(command, parameters, parse_yson=True)

        create_user(user)

        cell_id = get("//sys/@cell_id")
        new_leader_rpc_address = get_active_primary_master_follower_address(self)

        check_raises_wrapper = self._check_raises_superuser_permissions_required(_switch_leader)
        check_raises_wrapper(cell_id=cell_id, new_leader_address=new_leader_rpc_address, authenticated_user=user)

        self._create_and_update_aco(command, user)

        try:
            _switch_leader(cell_id=cell_id, new_leader_address=new_leader_rpc_address, authenticated_user=user)
        except Exception as e:
            print(str(e), file=sys.stderr)
            assert False, f"Should accept role {command}"
