
const TQ_PORT = 20000;

const MAX_DAYS = 31;
typedef int temperature;
typedef temperature month_temperatures<MAX_DAYS>;

program TQ_PROGRAM {
	version TQ_VERSION {
		month_temperatures TQPROC_CALL(int) = 0;
	} = 1;
} = 20000000;
