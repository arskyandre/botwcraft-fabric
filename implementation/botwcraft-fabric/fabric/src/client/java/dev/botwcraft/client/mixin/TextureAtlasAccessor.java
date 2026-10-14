package dev.botwcraft.client.mixin;

import java.util.Map;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.resources.Identifier;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(TextureAtlas.class)
public interface TextureAtlasAccessor {
	@Accessor("texturesByName")
	Map<Identifier, TextureAtlasSprite> botwcraft$sprites();

	@Accessor("width")
	int botwcraft$width();

	@Accessor("height")
	int botwcraft$height();
}
