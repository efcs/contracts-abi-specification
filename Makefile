# Makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?=
SPHINXBUILD  ?= sphinx-build
SOURCEDIR    = .
BUILDDIR     = _build

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile
# HTML build target
html:
	@$(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/html" $(SPHINXOPTS) $(O)
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/html."

# Clean build directory
clean:
	rm -rf $(BUILDDIR)/*

# Live reload for development (requires sphinx-autobuild)
livehtml:
	sphinx-autobuild -b html $(SOURCEDIR) $(BUILDDIR)/html

# Copy the generated HTML to  /efs/services/www/public/contracts.efcs.ca
release: html
	rsync -av --delete $(BUILDDIR)/html/ /efs/services/www/public/contracts-design.efcs.ca/


# Check for broken links
linkcheck:
	@$(SPHINXBUILD) -b linkcheck "$(SOURCEDIR)" "$(BUILDDIR)/linkcheck" $(SPHINXOPTS) $(O)
	@echo
	@echo "Link check complete; look for any errors in the above output " \
	      "or in $(BUILDDIR)/linkcheck/output.txt."

# Build man pages
man:
	@$(SPHINXBUILD) -b man "$(SOURCEDIR)" "$(BUILDDIR)/man" $(SPHINXOPTS) $(O)
	@echo
	@echo "Build finished. The manual pages are in $(BUILDDIR)/man."

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
