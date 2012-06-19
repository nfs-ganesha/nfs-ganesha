#!/usr/bin/perl

# example output from snmp:
# enterprises.12384.999.2.1.44.0 = STRING: "COMPONENT_FSAL_UP"
# enterprises.12384.999.2.1.44.1 = STRING: "Log level for this component"
# enterprises.12384.999.2.1.44.2.0 = STRING: "STRING"
# enterprises.12384.999.2.1.44.2.1 = STRING: "NIV_EVENT"


my $comm_query_component_ids = "snmpwalk -Os -c ganesha -v 1 localhost enterprises.12384.999";
my $comm_query_component = "snmpwalk -Os -c ganesha -v 1 localhost ";
my $comm_set_level = "snmpset -Os -c ganesha -v 1 localhost ";
my %component_ids;
sub get_component_ids {
    my @component_output = `${comm_query_component_ids}`;
    foreach(@component_output) {
	if ($_ =~ /(.+).0\s+\=\s+STRING:\s+\"(COMPONENT_\w+)\".*/) {
	    $component_ids{$2} = $1;
	}
	if ($_ =~ /(.+).0\s+\=\s+STRING:\s+\"(LOG_MESSAGE_\w+)\".*/) {
	    $component_ids{$2} = $1;
	}
    }
    break;
}

sub get_log_level {
    my $comp_id = shift(@_);
    my $comm = $comm_query_component . $comp_id;
    my @output = `${comm}`;
    foreach(@output) {
	if ($_ =~ /.*(NIV_\w+).*/) {
	    return $1;
	}
    }
    return "ERR_NOTFOUND";
}

sub get_log_level_id {
    my $comp_id = shift(@_);
    my $comm = $comm_query_component . $comp_id;
    my @output = `${comm}`;
    foreach(@output) {
	if ($_ =~ /(.+)\s+\=\s+STRING:\s+\"(NIV_\w+)\".*/) {
	    return ($1,$2);
	}
    }
    return "ERRNOTFOUND";
}

sub set_log_level {
    my $comp_id = shift(@_);
    my $log_level = shift(@_);
    my $log_level_id;
    my $curr_log_level;

    ($log_level_id, $curr_log_level) = get_log_level_id($comp_id);
    $curr_log_level or return 0;

    my $comm = $comm_set_level . "${log_level_id} s ${log_level}";
    my $output = `${comm} 2>&1`;
    if ($output =~ /.*Error.*/ || $output =~ /.*Fail.*/) {
	return 0;
    }
    return 1;
}

if ($ARGV[0] eq "list_components") {
    get_component_ids();
    print "COMPONENTS: \n----------------\n";
    foreach $key (keys %component_ids) {
	print "$key\n";
    }
}

if ($ARGV[0] eq "list_log_settings") {
    get_component_ids();
    foreach $key (keys %component_ids) {
	printf("%-30s : %-20s\n", $key, get_log_level($component_ids{$key}));
    }    
}

if ($ARGV[0] eq "set_log_level") {
    my $component = $ARGV[1];
    my $component_id;
    my $level = $ARGV[2];
    
    get_component_ids();
    $component_id = $component_ids{$component};
    if (! $component_id) {
	print "ERROR: Could not find component \"${component}\"\n";
	exit;
    }
    if (set_log_level($component_id, $level)) {
	print "Successfully set ${component} to ${level}\n";
    } else {
	print "Failed to set  ${component} to ${level}. Make sure ${level} is a valid log level.\n";
    }
}

if (@ARGV == 0) {
    print "${0} help\n";
    print "${0} list_components\n";
    print "${0} list_log_settings\n";
    print "${0} set_log_level COMPONENT LOGLEVEL\n\n";
}

if ($ARGV[0] eq "help") {
    print "${0} list_components\n";
    print "${0} list_log_settings\n";
    print "${0} set_log_level COMPONENT LOGLEVEL\n";

    print "\n\nEXAMPLES:\n
[root]# ${0} list_components
COMPONENTS: 
----------------
COMPONENT_FILEHANDLE
COMPONENT_NFS_CB
COMPONENT_RPC_CACHE
...

[root]# ${0} list_log_settings
COMPONENT_FILEHANDLE           : NIV_EVENT
COMPONENT_NFS_CB               : NIV_EVENT
COMPONENT_RPC_CACHE            : NIV_EVENT
...

[root]# ${0} set_log_level COMPONENT_FILEHANDLE NIV_DEBUG
Successfully set COMPONENT_FILEHANDLE to NIV_DEBUG

[root]# ${0} set_log_level COMPONENT_FILEHANDLE NIV_DEB
Failed to set  COMPONENT_FILEHANDLE to NIV_DEB. Make sure NIV_DEB is a valid log level.

[root]# ${0} set_log_level COMPONENT_FILEHANE NIV_DEBUG
ERROR: Could not find component \"COMPONENT_FILEHANE\"

[root]# ${0} list_log_settings
COMPONENT_FILEHANDLE           : NIV_DEBUG
COMPONENT_NFS_CB               : NIV_EVENT
COMPONENT_RPC_CACHE            : NIV_EVENT
...
";
}
