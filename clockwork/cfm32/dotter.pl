#!/usr/bin/perl

use File::Find;
use Digest::SHA qw/sha1_hex/;

sub esc
{
	map { 'x'.substr(sha1_hex($_), 0, 8) } @_;
}

print "digraph g {\n";
my $ROOT = $ENV{PWD};
find(sub {
		return unless -f;
		open my $fh, "<", $_
			or die "$File::Find::name: $!\n";
		my $file = $_;
		while (<$fh>) {
			next unless m/#include "(.*)"/;
			my $target = $1;
			my ($a, $b) = esc($file, $target);
			print qq($a [label="$file"]\n);
			print qq($b [label="$target"]\n);
			print "$a -> $b\n";
		}
		close $fh;
	}, @ARGV);
print "}\n";
