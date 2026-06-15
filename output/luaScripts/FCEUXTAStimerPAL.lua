--Made by Yave Yu

gui.register();

tcolor = ""
lcolor = ""
lpos = 7

while (true) do
	frame = movie.framecount() 
	lag = FCEU.lagcount()
	
	--Frame
	if movie.mode() == "record" then
		tcolor = "#FFF0C0FF"
	elseif movie.mode() == "playback" then
		tcolor = "#FFFFFFFF"
	else
		tcolor = "#00B0FFFF"
	end
	gui.text(2,1,string.format("F%d",frame),"#000000ff","#00000000")
	gui.text(1,2,string.format("F%d",frame),"#000000ff","#00000000")
	gui.text(2,2,string.format("F%d",frame),"#000000ff","#00000000")
	gui.text(1,1,string.format("F%d",frame),tcolor,"#00000000")
	
	--Lag
	if FCEU.lagged() then
		lcolor = "#FF0000FF"
	else
		lcolor = "#00FF00FF"
	end
	gui.text(lpos+string.len(string.format("F%d",frame))*6+1,1,string.format("L%d",lag),"#000000ff","#00000000")
	gui.text(lpos+string.len(string.format("F%d",frame))*6,2,string.format("L%d",lag),"#000000ff","#00000000")
	gui.text(lpos+string.len(string.format("F%d",frame))*6+1,2,string.format("L%d",lag),"#000000ff","#00000000")
	gui.text(lpos+string.len(string.format("F%d",frame))*6,1,string.format("L%d",lag),lcolor,"#00000000")
	
	--Timer
    timer = {on=true,w=58,h=7,toggle="numpad6"}
    if timer.on then
        mins = math.floor(movie.framecount()/3000)
        secs = movie.framecount()/50-mins*60
		gui.text(200,1,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#000000ff","#00000000")
		gui.text(199,2,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#000000ff","#00000000")
        gui.text(200,2,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#000000ff","#00000000")
        gui.text(199,1,string.format("%s:%05.2f",os.date("!%H:%M",mins*60),secs),"#ffffffff","#00000000")
    end
    FCEU.frameadvance();
end;