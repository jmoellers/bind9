#!/bin/bin/perl -w
#
# Copyright (C) Internet Systems Consortium, Inc. ("ISC")
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, you can obtain one at https://mozilla.org/MPL/2.0/.
#
# See the COPYRIGHT file distributed with this work for additional
# information regarding copyright ownership.

# Our SPNEGO implementation uses some functions generated by the
# Heimdal ASN.1 compiler, which this script then whacks a bit to make
# them work properly in this stripped down implementation.  We don't
# want to require our users to have a copy of the compiler, so we ship
# the output of this script, but we need to keep the script around in
# any case to cope with future changes to the SPNEGO ASN.1 code, so we
# might as well supply the script for users who want it.

# Overall plan: run the ASN.1 compiler, run each of its output files
# through indent, fix up symbols and whack everything to be static.
# We use indent for two reasons: (1) to whack the Heimdal compiler's
# output into something closer to ISC's coding standard, and (2) to
# make it easier for this script to parse the result.

# Output from this script is C code which we expect to be #included
# into another C file, which is why everything generated by this
# script is marked "static".  The intent is to minimize the number of
# extern symbols exported by the SPNEGO implementation, to avoid
# potential conflicts with the GSSAPI libraries.

###

# Filename of the ASN.1 specification.  Hardcoded for the moment
# since this script is intended for compiling exactly one module.

my $asn1_source = $ENV{ASN1_SOURCE} || "spnego.asn1";

# Heimdal ASN.1 compiler.  This script was written using the version
# from Heimdal 0.7.1.  To build this, download a copy of
# heimdal-0.7.1.tar.gz, configure and build with the default options,
# then look for the compiler in heimdal-0.7.1/lib/asn1/asn1_compile.

my $asn1_compile = $ENV{ASN1_COMPILE} || "asn1_compile";

# BSD indent program.  This script was written using the version of
# indent that comes with FreeBSD 4.11-STABLE.  The GNU project, as
# usual, couldn't resist the temptation to monkey with indent's
# command line syntax, so this probably won't work with GNU indent.

my $indent = $ENV{INDENT} || "indent";

###

# Step 1: run the compiler.  Input is the ASN.1 file.  Outputs are a
# header file (name specified on command line without the .h suffix),
# a file called "asn1_files" listing the names of the other output
# files, and a set of files containing C code generated by the
# compiler for each data type that the compiler found.

if (! -r $asn1_source || system($asn1_compile, $asn1_source, "asn1")) {
    die("Couldn't compile ASN.1 source file $asn1_source\n");
}

my @files = ("asn1.h");

open(F, "asn1_files")
    or die("Couldn't open asn1_files: $!\n");
push(@files, split)
    while (<F>);
close(F);

unlink("asn1_files");

###

# Step 2: generate header block.

print(q~/*
 * Copyright (C) 2006  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*! \file
 * \brief Method routines generated from SPNEGO ASN.1 module.
 * See spnego_asn1.pl for details.  Do not edit.
 */

~);

###

# Step 3: read and process each generated file, then delete it.

my $output;

for my $file (@files) {

    my $is_static = 0;

    system($indent, "-di1", "-ldi1", $file) == 0
	or die("Couldn't indent $file");

    unlink("$file.BAK");

    open(F, $file)
	or die("Couldn't open $file: $!");

    while (<F>) {

	# Symbol name fixups

	s/heim_general_string/general_string/g;
	s/heim_octet_string/octet_string/g;
	s/heim_oid/oid/g;
	s/heim_utf8_string/utf8_string/g;

	# Convert all externs to statics

	if (/^static/) {
	    $is_static = 1;
	}

	if (!/^typedef/ &&
	    !$is_static &&
	    /^[A-Za-z_][0-9A-Za-z_]*[ \t]*($|[^:0-9A-Za-z_])/) {
	    $_ = "static " . $_;
	    $is_static = 1;
	}

	if (/[{};]/) {
	    $is_static = 0;
	}

	# Suppress file inclusion, pass anything else through

	if (!/#include/) {
	    $output .= $_;
	}
    }

    close(F);
    unlink($file);
}

# Step 4: Delete unused stuff to avoid code bloat and compiler warnings.

my @unused_functions = qw(ContextFlags2int
			  int2ContextFlags
			  asn1_ContextFlags_units
			  length_NegTokenInit
			  copy_NegTokenInit
			  length_NegTokenResp
			  copy_NegTokenResp
			  length_MechTypeList
			  length_MechType
			  copy_MechTypeList
			  length_ContextFlags
			  copy_ContextFlags
			  copy_MechType);

$output =~ s<^static [^\n]+\n$_\(.+?^}></* unused function: $_ */\n>ms
    foreach (@unused_functions);

$output =~ s<^static .+$_\(.*\);$></* unused declaration: $_ */>m
    foreach (@unused_functions);

$output =~ s<^static struct units ContextFlags_units\[\].+?^};>
            </* unused variable: ContextFlags_units */>ms;

$output =~ s<^static int asn1_NegotiationToken_dummy_holder = 1;>
            </* unused variable: asn1_NegotiationToken_dummy_holder */>ms;

$output =~ s<^static void\nfree_ContextFlags\(ContextFlags \* data\)\n{\n>
            <$&\t(void)data;\n>ms;

# Step 5: Write the result.

print($output);

