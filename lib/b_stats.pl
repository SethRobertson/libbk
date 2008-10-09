#!/usr/bin/perl
# 
#
# 
#
# ++Copyright LIBBK++
#
# Copyright (c) 2003 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Mail <projectbaka@baka.org> for further information
#
# --Copyright LIBBK--
#
#
# Sort an HTML stats file by column number
#

use Getopt::Std;

my($Usage) = "Usage: $0: [-r] <-c column>\n";
getopts('rc:') || die $Usage;

$colnum = int($opt_c);
die $Usage unless ($colnum > 0);

$mult = -1;
$mult = 1 if ($opt_r);

$header = <STDIN>;
print $header;
@input = <STDIN>;
$footer = @input[$#input];
$#input--;
@output = sort { @a = split(/\<td[^>]*\>/,$a); @b = split(/\<td[^>]*\>/,$b); ($a[$colnum] <=> $b[$colnum]) * $mult; } @input;
print @output;
print $footer;
