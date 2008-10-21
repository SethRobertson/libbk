#!/usr/bin/perl
#
#
# Add stats tables produced by baka statistics, produce a combined
# output file as the sum of the inputs (for combining per-thread stats
# files)
#
#
# ++Copyright BAKA++
#
# Copyright © 2003-2008 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Send e-mail to <projectbaka@baka.org> for further information.
#
# - -Copyright BAKA- -
#

use Getopt::Std;

my($USAGE) = "Usage: $0  <statfile> ...\n";
getopts('c:') || die $Usage;

die $USAGE if ($#ARGV < 1);

$first = 1;
foreach $file (@ARGV)
{
  open(X,$file) || die "Cannot open $file\n";

  while (<X>)
  {
    my (@A) = split(/\<td[^>]*\>/);
    if ($#A == 7)
    {
      $state = 1;

      $A[1] =~ s/<.*//;
      $A[2] =~ s/<.*//;
      $name = "$A[1]<$A[2]";
      if (defined($min{$name}))
      {
	$min{$name} = min($min{$name},int($A[3]));
      }
      else
      {
	$min{$name} = int($A[3]);
      }
      if (defined($max{$name}))
      {
	$max{$name} = max($max{$name},int($A[5]));
      }
      else
      {
	$max{$name} = int($A[5]);
      }
      $count{$name} += int($A[6]);
      $sum{$name} += $A[7];
    }
    else
    {
      if ($first)
      {
	push(@trailer, $_) if ($state);
	push(@header, $_) unless ($state);
      }
    }
  }

  close(X);
  $first = 0;
}

print @header;
foreach $name (keys %count)
{
  my ($n1,$n2) = split(/</,$name);

  printf(qq^<tr><td>%s</td><td>%s</td><td align="right">%u</td><td align="right">%.3f</td><td align="right">%u</td><td align="right">%u</td><td align="right">%.6f</td></tr>\n^,
	 $n1, $n2, $min{$name}, $sum{$name}*1000000/$count{$name}, $max{$name}, $count{$name},$sum{$name});
}
print @trailer;


sub min($$)
{
  $_[0]>$_[1]?$_[1]:$_[0];
}
sub max($$)
{
  $_[0]<$_[1]?$_[1]:$_[0];
}
