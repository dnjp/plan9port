newwindow() {
	winctl=$(9p read acme/new/ctl)
	export winid=$(echo $winctl | awk '{print $1}')
}

winctl() {
	echo $* | 9p write acme/$winid/ctl
}

winread() {
	9p read acme/$winid/$1
}

winwrite() {
	9p write acme/$winid/$1
}

windump() {
	if ! $(test -z "$1" -o "$1" = "-"); then
		winctl dumpdir $1
	fi
	if ! $(test -z "$2" -o "$2" = "-"); then
		winctl dump $2
	fi
}

winname() {
	winctl name $1
}

winwriteevent() {
	echo $1$2$3 $4 | winwrite event
}

windel() {
	if [ "$1" = "sure" ]; then
		winctl delete
	else
		winctl del
	fi
}

wineventloop() {
	. <(winread event 2> /dev/null | acmeevent)
}
