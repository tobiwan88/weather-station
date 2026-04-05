/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/shell/shell.h>

#include <location_registry/location_registry.h>

/* --------------------------------------------------------------------------
 * location add <name>
 * -------------------------------------------------------------------------- */
static int cmd_add(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: location add <name>");
		return -EINVAL;
	}

	int rc = location_registry_add(argv[1]);

	if (rc == -EEXIST) {
		shell_warn(sh, "Location '%s' already exists", argv[1]);
		return 0;
	} else if (rc == -ENOMEM) {
		shell_error(sh, "Location registry full");
		return rc;
	} else if (rc == -ENAMETOOLONG) {
		shell_error(sh, "Name too long (max %d chars)", CONFIG_LOCATION_REGISTRY_NAME_LEN);
		return rc;
	} else if (rc != 0) {
		shell_error(sh, "Failed: %d", rc);
		return rc;
	}

	shell_print(sh, "Added location: %s", argv[1]);
	return 0;
}

/* --------------------------------------------------------------------------
 * location remove <name>
 * -------------------------------------------------------------------------- */
static int cmd_remove(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: location remove <name>");
		return -EINVAL;
	}

	int rc = location_registry_remove(argv[1]);

	if (rc == -ENOENT) {
		shell_error(sh, "Location '%s' not found", argv[1]);
		return rc;
	} else if (rc != 0) {
		shell_error(sh, "Failed: %d", rc);
		return rc;
	}

	shell_print(sh, "Removed location: %s", argv[1]);
	return 0;
}

/* --------------------------------------------------------------------------
 * location list
 * -------------------------------------------------------------------------- */
static int list_cb(const char *name, void *user_data)
{
	const struct shell *sh = user_data;

	shell_print(sh, "  %s", name);
	return 0;
}

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int count = location_registry_count();

	if (count == 0) {
		shell_print(sh, "(no locations defined)");
		return 0;
	}

	shell_print(sh, "Locations (%d):", count);
	location_registry_foreach(list_cb, (void *)sh);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_location, SHELL_CMD_ARG(add, NULL, "Add a location: add <name>", cmd_add, 2, 0),
	SHELL_CMD_ARG(remove, NULL, "Remove a location: remove <name>", cmd_remove, 2, 0),
	SHELL_CMD(list, NULL, "List all locations", cmd_list), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(location, &sub_location, "Location registry commands", NULL);
