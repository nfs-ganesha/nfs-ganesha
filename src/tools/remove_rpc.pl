#!/usr/bin/perl

use strict;

my $var = <<END;
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/types.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#endif
END

my $var2 = <<END;
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/types.h>
#include <gssrpc/svc.h>
#include <gssrpc/auth.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#include <rpc/auth.h>
#endif
END

my $var3 = <<END;
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/types.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
END

my $var4 = <<END;
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#endif
END

my $var5 = <<END;
#ifdef _USE_GSSRPC
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>

#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif
END

my $var6 = <<END;
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif
END

my $var7 = <<END;
#ifdef _USE_GSSRPC
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#include <gssrpc/rpc.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#endif
END

my $var8 = <<END;
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
END

foreach my $file (@ARGV) {
    open(IN, $file);
    undef $/;
    my $content = <IN>;
    close(IN);
    $content =~ s/$var/#include "rpc.h"/is;
    $content =~ s/$var2/#include "rpc.h"/is;
    $content =~ s/$var3/#include "rpc.h"/is;
    $content =~ s/$var4/#include "rpc.h"/is;
    $content =~ s/$var5/#include "rpc.h"/is;
    $content =~ s/$var6/#include "rpc.h"/is;
    $content =~ s/$var7/#include "rpc.h"/is;
    $content =~ s/$var8/#include "rpc.h"/is;

    open(OUT, ">$file");
    print OUT $content;
    close(OUT);
}


