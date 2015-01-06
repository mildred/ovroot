
all: ovroot

setuid: ovroot
	sudo sh -c 'chown 0:0 ovroot; chmod 4755 ovroot'

