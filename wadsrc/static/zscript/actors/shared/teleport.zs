
class TeleportDest : Actor
{
	default
	{
		+NOBLOCKMAP
		+NOSECTOR
		+DONTSPLASH
		+NOTONAUTOMAP
	}
}

class TeleportDest2 : TeleportDest
{
	default
	{
		+NOGRAVITY
	}
}

class TeleportDest3 : TeleportDest2
{
	default
	{
		-NOGRAVITY
	}
}

