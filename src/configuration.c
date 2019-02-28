#include "configuration.h"

#include <sys/socket.h>
#include <err.h>
#include <errno.h>
#include <jansson.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"
#include "csv.h"

#define OPTNAME_LISTEN		"listen"
#define OPTNAME_LISTEN_ADDRESS	"address"
#define OPTNAME_LISTEN_PORT	"port"
#define OPTNAME_VRPS_LOCATION	"vrpsLocation"
#define OPTNAME_RTR_INTERVAL	"rtrInterval"
#define OPTNAME_RTR_INTERVAL_REFRESH	"refresh"
#define OPTNAME_RTR_INTERVAL_RETRY	"retry"
#define OPTNAME_RTR_INTERVAL_EXPIRE	"expire"

#define DEFAULT_ADDR		NULL
#define DEFAULT_PORT		"323"
#define DEFAULT_VRPS		NULL
#define DEFAULT_REFRESH_INTERVAL		3600
#define DEFAULT_RETRY_INTERVAL		600
#define DEFAULT_EXPIRE_INTERVAL		7200

/* Protocol timing parameters ranges */
#define MIN_REFRESH_INTERVAL	1
#define MAX_REFRESH_INTERVAL	86400
#define MIN_RETRY_INTERVAL		1
#define MAX_RETRY_INTERVAL		7200
#define MIN_EXPIRE_INTERVAL		600
#define MAX_EXPIRE_INTERVAL		172800

struct rtr_config {
	/** The listener address of the RTR server. */
	struct addrinfo *address;
	/** Stored aside only for printing purposes. */
	char *port;
	/** VRPs (Validated ROA Payload) location */
	char *vrps_location;
	/** Intervals use at RTR v1 End of data PDU **/
	int refresh_interval;
	int retry_interval;
	int expire_interval;
} config;

static int handle_json(json_t *);
static int json_get_string(json_t *, char const *, char *, char const **);
static int json_get_int(json_t *, char const *, int, int *);
static int init_addrinfo(char const *, char const *);

int
config_init(char const *json_file_path)
{
	json_t *json_root;
	json_error_t json_error;
	int error;

	/*
	 * TODO What's the point of a default start if there's
	 * no vrps input?
	 */
	if (json_file_path == NULL)
		return init_addrinfo(DEFAULT_ADDR, DEFAULT_PORT);

	json_root = json_load_file(json_file_path, JSON_REJECT_DUPLICATES,
	    &json_error);
	if (json_root == NULL) {
		warnx("JSON error on line %d, column %d: %s",
		    json_error.line, json_error.column, json_error.text);
		return -ENOENT;
	}

	error = handle_json(json_root);

	json_decref(json_root);
	return error;
}

void
config_cleanup(void)
{
	if (config.address != NULL)
		freeaddrinfo(config.address);
	if (config.port != NULL)
		free(config.port);
	if (config.vrps_location != NULL)
		free(config.vrps_location);
}

static int
load_interval(json_t *parent, char const *name, int default_value,
    int *result, int min_value, int max_value)
{
	int error;

	error = json_get_int(parent, name, default_value, result);
	if (error) {
		err(error, "Invalid value for interval '%s'", name);
		return error;
	}

	if (*result < min_value || max_value < *result) {
		err(-EINVAL, "Interval '%s' (%d) out of range, must be from %d to %d",
		    name, *result, min_value, max_value);
		return -EINVAL;
	}

	return 0;
}

static int
handle_json(json_t *root)
{
	json_t *listen;
	json_t *interval;
	char const *address;
	char const *port;
	char const *vrps;
	int refresh_interval;
	int retry_interval;
	int expire_interval;
	int error;

	if (!json_is_object(root)) {
		warnx("The root of the JSON file is not a JSON object.");
		return -EINVAL;
	}

	listen = json_object_get(root, OPTNAME_LISTEN);
	if (listen != NULL) {
		if (!json_is_object(listen)) {
			warnx("The '%s' element is not a JSON object.",
			    OPTNAME_LISTEN);
			return -EINVAL;
		}

		error = json_get_string(listen, OPTNAME_LISTEN_ADDRESS,
		    DEFAULT_ADDR, &address);
		if (error)
			return error;

		error = json_get_string(listen, OPTNAME_LISTEN_PORT,
		    DEFAULT_PORT, &port);
		if (error)
			return error;

	} else {
		address = DEFAULT_ADDR;
		port = DEFAULT_PORT;
	}

	error = json_get_string(root, OPTNAME_VRPS_LOCATION,
			    DEFAULT_VRPS, &vrps);
	if (error)
		return error;
	config.vrps_location = str_clone(vrps);

	interval = json_object_get(root, OPTNAME_RTR_INTERVAL);
	if (interval != NULL) {
		if (!json_is_object(interval)) {
			warnx("The '%s' element is not a JSON object.",
			    OPTNAME_RTR_INTERVAL);
			return -EINVAL;
		}

		error = load_interval(interval, OPTNAME_RTR_INTERVAL_REFRESH,
		    DEFAULT_REFRESH_INTERVAL, &refresh_interval,
		    MIN_REFRESH_INTERVAL, MAX_REFRESH_INTERVAL);
		if (error)
			return error;

		error = load_interval(interval, OPTNAME_RTR_INTERVAL_RETRY,
		    DEFAULT_RETRY_INTERVAL, &retry_interval,
		    MIN_RETRY_INTERVAL, MAX_RETRY_INTERVAL);
		if (error)
			return error;

		error = load_interval(interval, OPTNAME_RTR_INTERVAL_EXPIRE,
		    DEFAULT_EXPIRE_INTERVAL, &expire_interval,
		    MIN_EXPIRE_INTERVAL, MAX_EXPIRE_INTERVAL);
		if (error)
			return error;

		config.refresh_interval = refresh_interval;
		config.retry_interval = retry_interval;
		config.expire_interval = expire_interval;
	} else {
		config.refresh_interval = DEFAULT_REFRESH_INTERVAL;
		config.retry_interval = DEFAULT_RETRY_INTERVAL;
		config.expire_interval = DEFAULT_EXPIRE_INTERVAL;
	}

	return init_addrinfo(address, port);
}

static int
json_get_string(json_t *parent, char const *name, char *default_value,
    char const **result)
{
	json_t *child;

	child = json_object_get(parent, name);
	if (child == NULL) {
		*result = default_value;
		return 0;
	}

	if (!json_is_string(child)) {
		warnx("The '%s' element is not a JSON string.", name);
		return -EINVAL;
	}

	*result = json_string_value(child);
	return 0;
}

static int
json_get_int(json_t *parent, char const *name, int default_value,
    int *result)
{
	json_t *child;

	child = json_object_get(parent, name);
	if (child == NULL) {
		*result = default_value;
		return 0;
	}

	if (!json_is_integer(child)) {
		warnx("The '%s' element is not a JSON integer.", name);
		return -EINVAL;
	}

	*result = json_integer_value(child);
	return 0;
}

static int
init_addrinfo(char const *hostname, char const *service)
{
	int error;
	struct addrinfo hints;

	memset(&hints, 0 , sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	/* hints.ai_socktype = SOCK_DGRAM; */
	hints.ai_flags |= AI_PASSIVE;

	error = getaddrinfo(hostname, service, &hints, &config.address);
	if (error) {
		warnx("Could not infer a bindable address out of address '%s' and port '%s': %s",
		    (hostname != NULL) ? hostname : "any", service,
		    gai_strerror(error));
		return error;
	}

	config.port = str_clone(service);
	return 0;
}

struct addrinfo const *
config_get_server_addrinfo(void)
{
	return config.address;
}

char const *
config_get_server_port(void)
{
	return config.port;
}

char const *
config_get_vrps_location(void)
{
	return config.vrps_location;
}

int
config_get_refresh_interval(void)
{
	return config.refresh_interval;
}

int
config_get_retry_interval(void)
{
	return config.retry_interval;
}

int
config_get_expire_interval(void)
{
	return config.expire_interval;
}
