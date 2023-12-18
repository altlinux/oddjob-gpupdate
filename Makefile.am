SUBDIRS = src

VERSION=$(shell grep ^Version: $(top_srcdir)/oddjob-gpupdate.spec | awk '{print $$NF}')
RELEASE=$(shell rpm -q --specfile $(top_srcdir)/oddjob-gpupdate.spec | awk '{print $$NF}')
TAG=v$(VERSION)
distdir=$(PACKAGE)-$(VERSION)

compare_versions:
	test $(VERSION) = `cat autoversion`
tag: compare_versions
	git tag $(TAG)

force-tag: compare_versions
	git tag -f $(TAG)

REPOSITORY=$(shell git config remote.upstream.url 2> /dev/null || /bin/pwd)
ARCHIVEOUTDIR=$(shell cd $(top_srcdir) && pwd)

archive:
	tmpdir=`mktemp -d /tmp/make_archive_XXXXXX`;\
	if test -d "$$tmpdir" ; then \
		cd $$tmpdir;\
		git clone $(REPOSITORY) $(PACKAGE);\
		cd $$tmpdir/$(PACKAGE);\
		git checkout $(TAG);\
		./autogen;\
		make dist;\
		mkdir .gear;\
		echo "tar: ." >.gear/rules;\
		git add .gear/rules;\
		gear-rpm -ba --commit;\
		cp -v $(distdir).tar.gz $(ARCHIVEOUTDIR)/;\
		rm -fr $$tmpdir;\
	fi
