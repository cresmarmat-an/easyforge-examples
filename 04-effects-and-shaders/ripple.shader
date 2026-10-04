-- Moves the element's picture up and down in waves that travel across it.
value Speed: number = 1
value Tint: color = #FFFFFF

function Pixel(input: PixelInput) returns color then
    constant wave = Sine(input.Position.X * 0.08 + input.Time * Speed * 3)
    return Sample(input.Content, input.Coordinates + vector2(0, wave * 0.02)) * Tint
end
