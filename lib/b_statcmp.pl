#!/usr/bin/perl
#
# Compare two stats tables produced by baka statistics, produce a
# combined output file with relative order positions and percentage
# change.
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

use Getopt::Std;

my($USAGE) = "Usage: $0 -c <col-(typ 7)> <statfile1> <statfile2>\n";
getopts('c:') || die $Usage;

die $USAGE if ($#ARGV != 1);
die $USAGE unless $opt_c;

open(X,$ARGV[0]) || die "Cannot open $ARGV[0]\n";
@old = <X>;
close(X);

open(X,$ARGV[1]) || die "Cannot open $ARGV[1]\n";
@new = <X>;
close(X);

sub sortem
{
  my ($colnum,@aa) = @_;

  my (@header,@trailer,@body);
  my ($line);

  my ($state) = 0;
  foreach $line (@aa)
  {
    my (@junk) = split(/\<td[^>]*\>/,$line);
    if ($#junk == 7)
    {
      $state = 1;
      push(@body,$line);
    }
    else
    {
      push(@trailer, $line) if ($state);
      push(@header, $line) unless ($state);
    }
  }
  my (@new) = sort { my(@a) = split(/\<td[^>]*\>/,$a); my(@b) = split(/\<td[^>]*\>/,$b); ($b[$colnum] <=> $a[$colnum]); } @body;
  (@header, @new, @trailer);
}

@old = sortem($opt_c,@old);
@new = sortem($opt_c,@new);


$state = 0;

$pos = 1;
foreach $line (@new)
{
  @comp = split(/\<td[^>]*\>/,$line);
  if ($#comp != 7)
  {
    next;
  }
  else
  {
    $name = "$comp[1]<$comp[2]";
    $comp[$opt_c] =~ s/\D//g;
    $nlen{$name} = ($comp[$opt_c]);
    $npos{$name} = $pos++;
    $nline{$name} = $line;
    $common{$name} = $nlen{$name};
  }
}

$pos = 1;
$state = 0;
foreach $line (@old)
{
  @comp = split(/\<td[^>]*\>/,$line);
  if ($#comp != 7)
  {
    if ($state == 0)
    {
      $line =~ s:</tr>:<th>Old</th><th>New</th><th>% Change</th></tr>:;
      $header .= $line;
    }
    else
    {
      $trailer .= $line;
    }
  }
  else
  {
    $state = 1;
    $name = "$comp[1]<$comp[2]";
    $comp[$opt_c] =~ s/\D//g;
    $olen{$name} = ($comp[$opt_c]);
    $opos{$name} = $pos++;
    $oline{$name} = $line;
    $common{$name} = $olen{$name};
  }
}

print $header;
foreach $name (sort { my ($ret) = $common{$b} <=> $common{$a}; if (!$ret) { $ret = $opos{$a} <=> $opos{$b}; } if (!$ret) { $ret = $npos{$a} <=> $npos{$b}; } $ret; } keys %common)
{
  if ($olen{$name} == 0 || $nlen{$name} == 0)
  {
    $perc = "N/A";
  }
  else
  {
    $perc = sprintf("%.2f",($nlen{$name}-$olen{$name})*100/$olen{$name});
  }

  $line = $oline{$name} || $nline{$name};

  $line =~ s:</tr>:<td align="right">$opos{$name}</td><td align="right">$npos{$name}</td><td align="right">$perc</td>:;
  print $line;
}
print $trailer;
