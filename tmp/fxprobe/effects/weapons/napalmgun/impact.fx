effect effects/weapons/napalmgun/impact
{
	size	320

	sound "sound"
	{
		soundShader	"weapon_napalmgun_explode"
	}
	shake "camerashake"
	{
		duration	0.35,0.35
		scale		2
		attenuateEmitter
		attenuation	200,850
	}
	decal "scorch"
	{

		decal
		{
			duration	0.002,0.002
			material	"textures/decals/rocketburn"

			start
			{
				size { point 100,100 }
				rotate { box 0,1 }
			}
		}
	}
	spawner "column_smoke"
	{
		count		6,6

		sprite
		{
			duration	0.5,1
			material	"gfx/effects/smoke/cloud_alpha2"
			gravity		-0.02,-0.02

			start
			{
				position { line 0,-10,0,180,10,0 linearSpacing }
				velocity { box 0,30,0,0,40,0 }
				size { box 120,120,160,160 }
				tint { point 0.482353,0.466667,0.419608 }
				fade { point 0 }
				rotate { box 0,1 }
			}

			motion
			{
				tint { envelope "linear" }
				fade { envelope "cosine" }
				rotate { envelope "linear" }
			}

			end
			{
				tint { point 0.168627,0.164706,0.141176 }
				fade { point 0.4 }
				rotate { box -0.277778,0.277778 relative }
			}
		}
	}
	spawner "smoke"
	{
		start		0.6,0.6
		count		3,3

		sprite
		{
			duration	2,2
			material	"gfx/effects/smoke/cloud_alpha"

			start
			{
				position { line 10,-30,-30,10,30,30 }
				velocity { box 0,0,0,20,0,0 }
				size { line 120,120,180,180 }
				tint { point 0.501961,0.501961,0.501961 }
				fade { point 0 }
				rotate { box 0,1 }
			}

			motion
			{
				fade { envelope "cosine" }
				rotate { envelope "linear" }
			}

			end
			{
				fade { point 0.1 }
				rotate { box -0.111111,0.111111 relative }
			}
		}
	}
	emitter "column_fire"
	{
		duration	1,1
		count		6,12

		sprite
		{
			duration	0.25,0.6
			material	"gfx/effects/fire/fire1a"

			start
			{
				position { line 0,-12,-12,10,12,12 linearSpacing }
				velocity { box 10,0,0,100,0,0 }
				angle { box 0,0,0,1,1,1 }
				size { line 50,50,75,75 }
				offset { box -2,-2,-2,2,2,2 }
				rotate { box 0,1 }
			}

			motion
			{
				size { envelope "linear" }
				tint { envelope "linear" }
				fade { envelope "linear" }
				angle { envelope "linear" }
				rotate { envelope "linear" }
			}

			end
			{
				size { line 60,60,80,80 }
				angle { box -1,-1,-1,1,1,1 relative }
				rotate { box -0.277778,0.277778 relative }
			}
		}
	}
	emitter "column_fire2"
	{
		duration	1,1
		count		6,12

		sprite
		{
			duration	0.25,0.65
			material	"gfx/effects/fire/fire4a"

			start
			{
				position { line 0,-12,-12,10,12,12 linearSpacing }
				velocity { box 10,0,0,100,0,0 }
				angle { box 0,0,0,1,1,1 }
				size { box 50,50,75,75 }
				offset { box -2,-2,-2,2,2,2 }
				rotate { box 0,1 }
			}

			motion
			{
				size { envelope "linear" }
				tint { envelope "linear" }
				fade { envelope "linear" }
				angle { envelope "linear" }
				rotate { envelope "linear" }
			}

			end
			{
				size { box 60,60,80,80 }
				angle { box -1,-1,-1,1,1,1 relative }
				rotate { box -0.277778,0.277778 relative }
			}
		}
	}
	sound "sound2"
	{
		soundShader	"weapon_napalmgun_explode"
	}
	emitter "fire_core"
	{
		duration	0.4,0.4
		count		30,35

		sprite
		{
			duration	0.25,0.5
			blend	add
			material	"gfx/effects/fire/fire4a"
			gravity		-0.02,0

			start
			{
				position { line 10,-40,-40,10,40,40 }
				velocity { box 50,-20,-20,125,20,20 }
				size { box 55,55,65,65 }
				rotate { box 0,1 }
			}

			motion
			{
				size { envelope "linear" }
				fade { envelope "linear" }
				rotate { envelope "linear" }
			}

			end
			{
				size { box 40,40,60,60 }
				rotate { box -0.138889,0.138889 relative }
			}
		}
	}
	emitter "bluefire_core"
	{
		duration	0.35,0.35
		count		30,35

		sprite
		{
			duration	0.25,0.5
			blend	add
			material	"gfx/effects/fire/fire2b"
			gravity		-0.02,0

			start
			{
				position { line 10,-40,-40,10,40,40 }
				velocity { box 25,-20,-20,100,20,20 }
				size { box 45,45,55,55 }
				rotate { box 0,1 }
			}

			motion
			{
				size { envelope "linear" }
				fade { envelope "linear" }
				rotate { envelope "linear" }
			}

			end
			{
				size { box 35,35,45,45 }
				rotate { box -0.208333,0.208333 relative }
			}
		}
	}
	spawner "flash2"
	{
		count		2,2

		sprite
		{
			duration	0.25,0.25
			material	"gfx/effects/weapons/flash"
			generatedNormal

			start
			{
				position { line -2,-2,-2,2,2,2 }
				size { line 65,65,80,80 }
				rotate { box 0,1 }
			}

			motion
			{
				size { envelope "linear" }
				tint { envelope "linear" }
				fade { envelope "linear" }
			}

			end
			{
				size { point 41,41 }
			}
		}
	}
	emitter "unnamed13"
	{
		duration	0.5,0.5
		count		7,7

		oriented
		{
			duration	0.75,0.75
			blend	add
			material	"gfx/effects/fire/fire1a"

			start
			{
				position { point 1,0,0 }
				size { point 175,175 }
				rotate { box 0,0,0,0,0,1 }
			}

			motion
			{
				size { envelope "linear" }
				fade { envelope "fullflicker" }
			}

			end
			{
				size { point 75,75 }
			}
		}
	}
}





















