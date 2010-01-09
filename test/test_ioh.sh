#!/usr/bin/perl
#
#
# ++Copyright BAKA++
#
# Copyright © 2001-2010 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Send e-mail to <projectbaka@baka.org> for further information.
#
# - -Copyright BAKA- -
#
#



$receive{'ttcp'} = 'ttcp -p $port -r > /proj/startide/seth/big.out';
$trans{'ttcp'} = 'ttcp -p $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'bdttcp'} = 'bdttcp -p $port -r 127.1 < /dev/null > /proj/startide/seth/big.out';
$trans{'bdttcp'} = 'bdttcp -p $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'test_ioh'} = 'test_ioh --ioh-inbuf-hint=8192 -p $port -r < /dev/null > /proj/startide/seth/big.out';
$trans{'test_ioh'} = 'test_ioh --ioh-inbuf-hint=8192 -p $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'test_ioh-S'} = 'test_ioh --ioh-inbuf-hint=8192 -Sp $port -r < /dev/null > /proj/startide/seth/big.out';
$trans{'test_ioh-S'} = 'test_ioh --ioh-inbuf-hint=8192 -Sp $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'test_ioh-Sl'} = 'test_ioh --ioh-inbuf-hint=8192 -Sp $port -r < /dev/null > /proj/startide/seth/big.out';
$trans{'test_ioh-Sl'} = 'test_ioh --ioh-outbuf-max=20000 --ioh-inbuf-hint=8192 -Sp $port -t 127.1 < /proj/startide/seth/big.in';

$ENV{'port'}=6010;

foreach $cmd ('ttcp','ttcp','ttcp','bdttcp','bdttcp','bdttcp','test_ioh','test_ioh','test_ioh','test_ioh-S','test_ioh-S','test_ioh-S','test_ioh-Sl','test_ioh-Sl','test_ioh-Sl')
{
  print "$cmd $receive{$cmd} &\n";
  system("$receive{$cmd} &");
  sleep(5);
  system("time $trans{$cmd}");
  $ENV{'port'}++;
}
