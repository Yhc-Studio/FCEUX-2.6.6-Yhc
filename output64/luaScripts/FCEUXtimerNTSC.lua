--Made by Yave Yu

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