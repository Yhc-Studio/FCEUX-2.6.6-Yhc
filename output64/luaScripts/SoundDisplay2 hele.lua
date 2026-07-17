-- feos, 2012
-- gui.box frame simulates transparency
-- modified by Yave Yu, 2017

print("Hi-hat and keys may glitch if you produce sound effects.")
print(" ")
print("And praise Gocha!")

iterator = 15
kb = {x=8, y=153, on=true}
prev_keys = input.get()
semitones = {"A","A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"}

volumes = {
	S1V = {0}, S1C = {},
	S2V = {0}, S2C = {},
	TV = {0},
	NV = {0}, NC = {},
	DPCMV = {0}
}

function Draw()
	snd = sound.get()
	keys = input.get()
	
	-- do only at the first frame
	if #volumes.S1V == 1 then
		channels = {
			Square1  = {x=1,      y=9, vol=volumes.S1V, color=volumes.S1C, duty=0, midi=0, semitone=0, octave=0, prev_semitone=0, float = {}},
			Square2  = {x=1+45*1, y=9, vol=volumes.S2V, color=volumes.S2C, duty=0, midi=0, semitone=0, octave=0, prev_semitone=0, float = {}},
			Triangle = {x=1+45*2, y=9, vol=volumes.TV, midi=0, semitone=0, octave=0, prev_semitone=0, float = {}},
			Noise    = {x=1+45*3, y=9, vol=volumes.NV, color=volumes.NC, duty=0, midi=0, semitone=0, octave=0},
			DPCM     = {x=1+45*4, y=9, vol=volumes.DPCMV}
		}
	end

	-- update the first indices for volume tables
	-- shift the previous ones farther
	table.insert(channels.Square1.vol,  1, snd.rp2a03.square1.volume*15)
	table.insert(channels.Square2.vol,  1, snd.rp2a03.square2.volume*15)
	table.insert(channels.Triangle.vol, 1, snd.rp2a03.triangle.volume*15)
	table.insert(channels.Noise.vol,    1, snd.rp2a03.noise.volume*15)
	table.insert(channels.DPCM.vol,     1, snd.rp2a03.dpcm.volume*15)

	-- get duty and midikey for proper channels
	channels.Square1.duty = snd.rp2a03.square1.duty
	channels.Square2.duty = snd.rp2a03.square2.duty
	if snd.rp2a03.noise.short then
		channels.Noise.duty = 1
	else
		channels.Noise.duty = 0
	end
	
	channels.Square1.freq = snd.rp2a03.square1.frequency
	if channels.Square1.freq > 100000 then
		channels.Square1.freq = 0
	end
	channels.Square2.freq = snd.rp2a03.square2.frequency
	if channels.Square2.freq > 100000 then
		channels.Square2.freq = 0
	end
	channels.Triangle.freq = snd.rp2a03.triangle.frequency
	if channels.Triangle.freq > 50000 then
		channels.Triangle.freq = 0
	end
	channels.Noise.freq = snd.rp2a03.noise.regs.frequency
	
	channels.Square1.midi  = snd.rp2a03.square1.midikey+0.5
	channels.Square2.midi  = snd.rp2a03.square2.midikey+0.5
	channels.Triangle.midi = snd.rp2a03.triangle.midikey+0.5
	channels.Noise.midi    = snd.rp2a03.noise.midikey

	-- guess notes
	for name, chan in pairs(channels) do
		if name == "Square1" or name == "Square2" or name == "Triangle" or name == "Noise" then
			if chan.vol[1] > 0 then
				chan.octave = math.floor((chan.midi-12) / 12)
				chan.semitone = tostring(semitones[math.floor((chan.midi - 21) % 12)+1])
			else chan.semitone = "--"; chan.octave = "-"
			end
		end
	end
	
	-- notes display
	gui.text(kb.x+203, kb.y+1, "S1: "..channels.Square1.semitone..channels.Square1.octave,   "#c82000ff", "#000000ff")
	gui.text(kb.x+203, kb.y+9, string.format("%.1f",channels.Square1.freq),   "#c82000ff", "#000000ff")
	gui.text(kb.x+203, kb.y+17,  "S2: "..channels.Square2.semitone..channels.Square2.octave,   "#00b0f8ff", "#000000ff")
	gui.text(kb.x+203, kb.y+25, string.format("%.1f",channels.Square2.freq),   "#00b0f8ff", "#000000ff")
	gui.text(kb.x+203, kb.y+33, "TR: "..channels.Triangle.semitone..channels.Triangle.octave, "#00e070ff", "#000000ff")
	gui.text(kb.x+203, kb.y+41, string.format("%.1f",channels.Triangle.freq),   "#00e070ff", "#000000ff")
	gui.text(kb.x+203, kb.y+49, "NS: "..channels.Noise.freq, "#e0d000ff", "#000000ff")
	gui.text(kb.x+203, kb.y+57, string.format("%.1f",snd.rp2a03.noise.frequency),   "#e0d000ff", "#000000ff")
	--"#000000ff","#00000000"
-----------------
-- Draw hi-hat --
-----------------

	xhh1 = 227
	yhh1 = 18
	xhh2 = 227
	yhh2 = 18
	
	if channels.Noise.vol[1] > 0 then 
		if channels.Noise.octave >= 9 and channels.Noise.octave <= 12 then
			colorhh = "#ffaa00"
			if channels.Noise.vol[2] - channels.Noise.vol[1] < 4
			and channels.Noise.vol[2] > 0
			then yhh1 = 15
			end
		end
	else colorhh = "#00000000"
	end

	gui.line(xhh1-1,  yhh1,   xhh1+28, yhh1,   "#00000088")
	gui.line(xhh1-1,  yhh1-1, xhh1+28, yhh1-1, "#00000088")
	gui.line(xhh1-1,  yhh1-2, xhh1+28, yhh1-2, "#00000088")
	gui.line(xhh1+3,  yhh1-3, xhh1+24, yhh1-3, "#00000088")
	gui.line(xhh1+8,  yhh1-4, xhh1+19, yhh1-4, "#00000088")
	gui.line(xhh1+11, yhh1-5, xhh1+16, yhh1-5, "#00000088")
	gui.line(xhh1+12, yhh1-6, xhh1+15, yhh1-6, "#00000088")	
	gui.line(xhh2-1,  yhh2,   xhh2+28, yhh2,   "#00000088")
	gui.line(xhh2-1,  yhh2+1, xhh2+28, yhh2+1, "#00000088")
	gui.line(xhh2-1,  yhh2+2, xhh2+28, yhh2+2, "#00000088")
	gui.line(xhh2+3,  yhh2+3, xhh2+24, yhh2+3, "#00000088")
	gui.line(xhh2+8,  yhh2+4, xhh2+19, yhh2+4, "#00000088")
	gui.line(xhh2+11, yhh2+5, xhh2+16, yhh2+5, "#00000088")
	gui.line(xhh2+12, yhh2+6, xhh2+15, yhh2+6, "#00000088")

	gui.line(xhh1,    yhh1-1, xhh1+27, yhh1-1, colorhh)
	gui.line(xhh1+4,  yhh1-2, xhh1+23, yhh1-2, colorhh)
	gui.line(xhh1+9,  yhh1-3, xhh1+18, yhh1-3, colorhh)
	gui.line(xhh1+12, yhh1-4, xhh1+15, yhh1-4, colorhh)
	gui.line(xhh1+13, yhh1-5, xhh1+14, yhh1-5, colorhh)	
	gui.line(xhh2,    yhh2+1, xhh2+27, yhh2+1, colorhh)
	gui.line(xhh2+4,  yhh2+2, xhh2+23, yhh2+2, colorhh)
	gui.line(xhh2+9,  yhh2+3, xhh2+18, yhh2+3, colorhh)
	gui.line(xhh2+12, yhh2+4, xhh2+15, yhh2+4, colorhh)
	gui.line(xhh2+13, yhh2+5, xhh2+14, yhh2+5, colorhh)		
--------------------
-- Keyboard stuff --
--------------------

	if (kb.on) then	
		-- draw the kayboard
		gui.box(kb.x-8, kb.y, kb.x+200, kb.y+16, "#f8f8f8ff") -- white solid box
		for a = -2, 49 do gui.box(kb.x+4*a, kb.y, kb.x+4*a, kb.y+16, "#00000000") end -- black lines
		-- draw colored boxes as clean notes
		for name, chan in pairs(channels) do
			if name == "Square1" or name == "Square2" or name == "Triangle" then
				if name == "Triangle" then color = "#00e070ff"
				elseif name == "Square1" then color = "#c82000ff"
				else color = "#00b0f8ff"
				end
				
				if     chan.semitone == "C" then gui.box(kb.x+1 +28*(chan.octave-1), kb.y, kb.x+3 +28*(chan.octave-1), kb.y+16, color)
				elseif chan.semitone == "D" then gui.box(kb.x+5 +28*(chan.octave-1), kb.y, kb.x+7 +28*(chan.octave-1), kb.y+16, color)
				elseif chan.semitone == "E" then gui.box(kb.x+9 +28*(chan.octave-1), kb.y, kb.x+11+28*(chan.octave-1), kb.y+16, color)
				elseif chan.semitone == "F" then gui.box(kb.x+13+28*(chan.octave-1), kb.y, kb.x+15+28*(chan.octave-1), kb.y+16, color)
				elseif chan.semitone == "G" then gui.box(kb.x+17+28*(chan.octave-1), kb.y, kb.x+19+28*(chan.octave-1), kb.y+16, color)
				elseif chan.semitone == "A" then gui.box(kb.x+21+28*(chan.octave-1), kb.y, kb.x+23+28*(chan.octave-1), kb.y+16, color)
				elseif chan.semitone == "B" then gui.box(kb.x+25+28*(chan.octave-1), kb.y, kb.x+27+28*(chan.octave-1), kb.y+16, color)
				end
			end
		end		
		-- draw accidental keys
		gui.box(kb.x-3, kb.y, kb.x-5, kb.y+10, "#00000000")		
		for oct = 0, 6 do
			gui.box(kb.x+3+28*oct,  kb.y, kb.x+5+28*oct,  kb.y+10, "#00000000")
			gui.box(kb.x+7+28*oct,  kb.y, kb.x+9+28*oct,  kb.y+10, "#00000000")
			gui.box(kb.x+15+28*oct, kb.y, kb.x+17+28*oct, kb.y+10, "#00000000")
			gui.box(kb.x+19+28*oct, kb.y, kb.x+21+28*oct, kb.y+10, "#00000000")
			gui.box(kb.x+23+28*oct, kb.y, kb.x+25+28*oct, kb.y+10, "#00000000")			
		end
		-- draw colored boxes over accidental keys
		for name, chan in pairs(channels) do			
			if name == "Square1" or name == "Square2" or name == "Triangle" then
				if name == "Triangle" then color = "#00e070ff"
				elseif name == "Square1" then color = "#c82000ff"
				else color = "#00b0f8ff"
				end
				
				if     chan.semitone == "C#" then gui.box(kb.x+3 +28*(chan.octave-1), kb.y    , kb.x+5 +28*(chan.octave-1), kb.y+10, color)
				elseif chan.semitone == "D#" then gui.box(kb.x+7 +28*(chan.octave-1), kb.y, kb.x+9 +28*(chan.octave-1), kb.y+10, color)
				elseif chan.semitone == "F#" then gui.box(kb.x+15+28*(chan.octave-1), kb.y, kb.x+17+28*(chan.octave-1), kb.y+10, color)
				elseif chan.semitone == "G#" then gui.box(kb.x+19+28*(chan.octave-1), kb.y, kb.x+21+28*(chan.octave-1), kb.y+10, color)
				elseif chan.semitone == "A#" then gui.box(kb.x+23+28*(chan.octave-1), kb.y, kb.x+25+28*(chan.octave-1), kb.y+10, color)
				end
			end
		end
		
		grey = "#707070ff"	
		for oct = 0, 6 do					
			gui.line(kb.x+3+28*oct,  kb.y+10, kb.x+5+28*oct,  kb.y+10, grey)
			gui.line(kb.x+7+28*oct,  kb.y+10, kb.x+9+28*oct,  kb.y+10, grey)
			gui.line(kb.x+15+28*oct, kb.y+10, kb.x+17+28*oct, kb.y+10, grey)
			gui.line(kb.x+19+28*oct, kb.y+10, kb.x+21+28*oct, kb.y+10, grey)
			gui.line(kb.x+23+28*oct, kb.y+10, kb.x+25+28*oct, kb.y+10, grey)
		end
		gui.line(kb.x-3,   kb.y+10, kb.x-5,   kb.y+10, grey)
		gui.line(kb.x-8,   kb.y,    kb.x+200, kb.y,    "#00000080")
		gui.line(kb.x-8,   kb.y+16, kb.x+200, kb.y+16, "#00000080")
		gui.line(kb.x-8,   kb.y,    kb.x-8,   kb.y+16, "#00000080")
		gui.line(kb.x+200, kb.y,    kb.x+200, kb.y+16, "#00000080")
	else
	end

--------------------
-- Floating notes --
--------------------
	
	if (kb.on) then
		for name, chan in pairs(channels) do
			if name == "Square1" or name == "Square2" or name == "Triangle" then
				if chan.prev_semitone ~= chan.semitone then
					if     chan.semitone == "C"  then table.insert(chan.float, 1, kb.x+1 +28*(chan.octave-1))
					elseif chan.semitone == "D"  then table.insert(chan.float, 1, kb.x+5 +28*(chan.octave-1))
					elseif chan.semitone == "E"  then table.insert(chan.float, 1, kb.x+9 +28*(chan.octave-1))
					elseif chan.semitone == "F"  then table.insert(chan.float, 1, kb.x+13+28*(chan.octave-1))
					elseif chan.semitone == "G"  then table.insert(chan.float, 1, kb.x+17+28*(chan.octave-1))
					elseif chan.semitone == "A"  then table.insert(chan.float, 1, kb.x+21+28*(chan.octave-1))
					elseif chan.semitone == "B"  then table.insert(chan.float, 1, kb.x+25+28*(chan.octave-1))
					elseif chan.semitone == "C#" then table.insert(chan.float, 1, kb.x+3 +28*(chan.octave-1))
					elseif chan.semitone == "D#" then table.insert(chan.float, 1, kb.x+7 +28*(chan.octave-1))
					elseif chan.semitone == "F#" then table.insert(chan.float, 1, kb.x+15+28*(chan.octave-1))
					elseif chan.semitone == "G#" then table.insert(chan.float, 1, kb.x+19+28*(chan.octave-1))
					elseif chan.semitone == "A#" then table.insert(chan.float, 1, kb.x+23+28*(chan.octave-1))
					end
				end
				
				if name == "Triangle" then color = "#00e070ff"
				elseif name == "Square1" then color = "#c82000ff"
				else color = "#00b0f8ff"
				end
			
				if #chan.float < 15 then
					for i = 2, #chan.float do
						if movie.framecount()%3 == 0 then gui.box(chan.float[i]-1, 160+i*5, chan.float[i]+3, 164+i*5, "#ffffff00") end
						gui.box(chan.float[i], 161+i*5, chan.float[i]+2, 163+i*5, color)
					end
				else
					for i = 2, 15 do
						if movie.framecount()%3 == 0 then gui.box(chan.float[i]-1, 160+i*5, chan.float[i]+3, 164+i*5, "#ffffff00") end
						gui.box(chan.float[i], 161+i*5, chan.float[i]+2, 163+i*5, color)
					end
					table.remove(chan.float, 16)
				end
			end
		end
	end

---------------------
-- Volumes display --
---------------------

	numberp = 38
	for name, chan in pairs(channels) do
		if name == "Square1" or name == "Square2" then
			-- set color for each duty value
			if chan.duty == 0 then table.insert(chan.color,1,"#a0f8a0ff")
			elseif chan.duty == 1 then table.insert(chan.color,1,"#00e000ff")
			elseif chan.duty == 2 then table.insert(chan.color,1,"#00d0e0ff")
			else table.insert(chan.color,1,"#008000ff")
			end
		elseif name == "Noise" then
			if chan.duty == 1 then
				table.insert(chan.color,1,"#c0c0c0ff")
			else
				table.insert(chan.color,1,"#e0d000ff")
			end
		end
		-- draw volumes
		gui.text(chan.x, 9, name, "#f8f8f8ff", "#000000ff")	
		if iterator <=14 then
			-- draw just first volume values
			gui.text(chan.x, chan.y+9+1, chan.vol[1], "#f8f8f8ff", "#000000ff")
			if tonumber(chan.vol[1]) > 0 then
				for j = 0, chan.vol[1]-1 do
					gui.box(chan.x+13+j*2, chan.y+9, chan.x+15+j*2, chan.y+8+9, "#000000ff")
					gui.line(chan.x+14+j*2, chan.y+1+9, chan.x+14+j*2, chan.y+7+9, "#80d8ffff")
					if name == "Square1" or name == "Square2" or name == "Noise" then
						-- color comes from duty
						if name == "Square1" or name == "Square2" then
							numberp = 38
						elseif name == "Noise" then
							numberp = 24
						end
						gui.text(chan.x+numberp, chan.y, chan.duty, chan.color[1], "#000000ff")
						gui.line(chan.x+14+j*2, chan.y+1+9, chan.x+14+j*2, chan.y+7+9, chan.color[1])
					end
				end
			end
		else
			-- draw all 15 volume values
			for i = 1, #chan.vol do
				gui.text(chan.x, chan.y+i*9+1, chan.vol[i], "#f8f8f8ff", "#000000ff")
				if tonumber(chan.vol[i]) > 0 then
					for j = 0, chan.vol[i]-1 do
						gui.box(chan.x+13+j*2, chan.y+i*9, chan.x+15+j*2, chan.y+8+i*9, "#000000ff")
						gui.line(chan.x+14+j*2, chan.y+1+i*9, chan.x+14+j*2, chan.y+7+i*9, "#80d8ffff")
						if name == "Square1" or name == "Square2" or name == "Noise" then
							-- color comes from duty
							if name == "Square1" or name == "Square2" then
								numberp = 38
							elseif name == "Noise" then
								numberp = 24
							end
							gui.text(chan.x+numberp, chan.y, chan.duty, chan.color[1], "#000000ff")
							gui.text(chan.x+numberp, chan.y, chan.duty, chan.color[1], "#000000ff")
							gui.line(chan.x+14+j*2, chan.y+1+i*9, chan.x+14+j*2, chan.y+7+i*9, chan.color[i])
						end
					end
				end
			end
		end
		-- keep the table limited
		table.remove(chan.vol, 15)
		
		-- highlight the first values
		-- 30 Hz blinking, works properly if your monitor is set to 60 Hz
		if chan.vol[1] > 0 and movie.framecount() % 3 == 0 then
			gui.box(chan.x+12, chan.y+8, chan.x+14+chan.vol[1]*2, chan.y+18, "#ffffff00")
		end
	end	
	
	for name, chan in pairs(channels) do
		if name == "Square1" or name == "Square2" or name == "Triangle" then
			chan.prev_semitone = chan.semitone
		end
	end
	
	prev_keys = keys
end
emu.registerafter(Draw);

gui.register();

while (true) do
    timer = {on=true,w=58,h=7,toggle="numpad6"}
    if timer.on then
        mins = math.floor(movie.framecount()/3606)
        secs = movie.framecount()/60.1-mins*60
		gui.text(200,1,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#000000ff","#00000000")
		gui.text(199,2,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#000000ff","#00000000")
        gui.text(200,2,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#000000ff","#00000000")
        gui.text(199,1,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#ffffffff","#00000000")
    end
    FCEU.frameadvance();
end;

